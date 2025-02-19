/*
   Copyright 2020-2021 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "processor.hpp"

#include <cassert>

#include <silkworm/chain/dao.hpp>
#include <silkworm/chain/intrinsic_gas.hpp>
#include <silkworm/chain/protocol_param.hpp>
#include <silkworm/trie/vector_root.hpp>

namespace silkworm {

ExecutionProcessor::ExecutionProcessor(const Block& block, State& state, const ChainConfig& config)
    : state_{state}, evm_{block, state_, config} {}

ValidationResult ExecutionProcessor::validate_transaction(const Transaction& txn) const noexcept {
    assert(pre_validate_transaction(txn, evm_.block().header.number, evm_.config(),
                                    evm_.block().header.base_fee_per_gas) == ValidationResult::kOk);

    if (!txn.from.has_value()) {
        return ValidationResult::kMissingSender;
    }

    const uint64_t nonce{state_.get_nonce(*txn.from)};
    if (nonce != txn.nonce) {
        return ValidationResult::kWrongNonce;
    }

    // https://github.com/ethereum/EIPs/pull/3594
    const intx::uint512 max_gas_cost{intx::umul(intx::uint256{txn.gas_limit}, txn.max_fee_per_gas)};
    // See YP, Eq (57) in Section 6.2 "Execution"
    const intx::uint512 v0{max_gas_cost + txn.value};
    if (state_.get_balance(*txn.from) < v0) {
        return ValidationResult::kInsufficientFunds;
    }

    if (available_gas() < txn.gas_limit) {
        // Corresponds to the final condition of Eq (58) in Yellow Paper Section 6.2 "Execution".
        // The sum of the transaction’s gas limit and the gas utilized in this block prior
        // must be no greater than the block’s gas limit.
        return ValidationResult::kBlockGasLimitExceeded;
    }

    return ValidationResult::kOk;
}

Receipt ExecutionProcessor::execute_transaction(const Transaction& txn) noexcept {
    assert(validate_transaction(txn) == ValidationResult::kOk);

    state_.clear_journal_and_substate();

    assert(txn.from.has_value());
    state_.access_account(*txn.from);

    const intx::uint256 base_fee_per_gas{evm_.block().header.base_fee_per_gas.value_or(0)};
    const intx::uint256 effective_gas_price{txn.effective_gas_price(base_fee_per_gas)};
    state_.subtract_from_balance(*txn.from, txn.gas_limit * effective_gas_price);

    if (txn.to.has_value()) {
        state_.access_account(*txn.to);
        // EVM itself increments the nonce for contract creation
        state_.set_nonce(*txn.from, txn.nonce + 1);
    }

    for (const AccessListEntry& ae : txn.access_list) {
        state_.access_account(ae.account);
        for (const evmc::bytes32& key : ae.storage_keys) {
            state_.access_storage(ae.account, key);
        }
    }

    const evmc_revision rev{evm_.revision()};

    const intx::uint128 g0{intrinsic_gas(txn, rev >= EVMC_HOMESTEAD, rev >= EVMC_ISTANBUL)};
    assert(g0 <= UINT64_MAX);  // true due to the precondition (transaction must be valid)

    const CallResult vm_res{evm_.execute(txn, txn.gas_limit - static_cast<uint64_t>(g0))};

    const uint64_t gas_used{txn.gas_limit - refund_gas(txn, vm_res.gas_left)};

    // award the miner
    const intx::uint256 priority_fee_per_gas{txn.priority_fee_per_gas(base_fee_per_gas)};
    state_.add_to_balance(evm_.block().header.beneficiary, gas_used * priority_fee_per_gas);

    state_.destruct_suicides();
    if (rev >= EVMC_SPURIOUS_DRAGON) {
        state_.destruct_touched_dead();
    }

    state_.finalize_transaction();

    cumulative_gas_used_ += gas_used;

    return {
        txn.type,                       // type
        vm_res.status == EVMC_SUCCESS,  // success
        cumulative_gas_used_,           // cumulative_gas_used
        logs_bloom(state_.logs()),      // bloom
        state_.logs(),                  // logs
    };
}

uint64_t ExecutionProcessor::available_gas() const noexcept {
    return evm_.block().header.gas_limit - cumulative_gas_used_;
}

uint64_t ExecutionProcessor::refund_gas(const Transaction& txn, uint64_t gas_left) noexcept {
    const evmc_revision rev{evm_.revision()};
    uint64_t refund{state_.get_refund()};
    if (rev < EVMC_LONDON) {
        refund += fee::kRSelfDestruct * state_.number_of_self_destructs();
    }
    const uint64_t max_refund_quotient{rev >= EVMC_LONDON ? param::kMaxRefundQuotientLondon
                                                          : param::kMaxRefundQuotientFrontier};
    const uint64_t max_refund{(txn.gas_limit - gas_left) / max_refund_quotient};
    refund = std::min(refund, max_refund);
    gas_left += refund;

    const intx::uint256 base_fee_per_gas{evm_.block().header.base_fee_per_gas.value_or(0)};
    const intx::uint256 effective_gas_price{txn.effective_gas_price(base_fee_per_gas)};
    state_.add_to_balance(*txn.from, gas_left * effective_gas_price);

    return gas_left;
}

ValidationResult ExecutionProcessor::execute_block_no_post_validation(std::vector<Receipt>& receipts) noexcept {
    receipts.clear();
    receipts.reserve(evm_.block().transactions.size());

    uint64_t block_num{evm_.block().header.number};
    if (block_num == evm_.config().dao_block) {
        dao::transfer_balances(state_);
    }

    cumulative_gas_used_ = 0;
    for (const Transaction& txn : evm_.block().transactions) {
        const ValidationResult err{validate_transaction(txn)};
        if (err != ValidationResult::kOk) {
            return err;
        }
        receipts.push_back(execute_transaction(txn));
    }

    apply_rewards();

    return ValidationResult::kOk;
}

ValidationResult ExecutionProcessor::execute_and_write_block(std::vector<Receipt>& receipts) noexcept {
    if (const ValidationResult res{execute_block_no_post_validation(receipts)}; res != ValidationResult::kOk) {
        return res;
    }

    const auto& header{evm_.block().header};

    if (cumulative_gas_used() != header.gas_used) {
        return ValidationResult::kWrongBlockGas;
    }

    if (evm_.revision() >= EVMC_BYZANTIUM) {
        static constexpr auto kEncoder = [](Bytes& to, const Receipt& r) { rlp::encode(to, r); };
        evmc::bytes32 receipt_root{trie::root_hash(receipts, kEncoder)};
        if (receipt_root != header.receipts_root) {
            return ValidationResult::kWrongReceiptsRoot;
        }
    }

    Bloom bloom{};  // zero initialization
    for (const Receipt& receipt : receipts) {
        join(bloom, receipt.bloom);
    }
    if (bloom != header.logs_bloom) {
        return ValidationResult::kWrongLogsBloom;
    }

    state_.write_to_db(header.number);

    return ValidationResult::kOk;
}

void ExecutionProcessor::apply_rewards() noexcept {
    const evmc_revision rev{evm_.revision()};
    intx::uint256 block_reward;
    if (rev >= EVMC_CONSTANTINOPLE) {
        block_reward = param::kBlockRewardConstantinople;
    } else if (rev >= EVMC_BYZANTIUM) {
        block_reward = param::kBlockRewardByzantium;
    } else {
        block_reward = param::kBlockRewardFrontier;
    }

    const uint64_t block_number{evm_.block().header.number};
    intx::uint256 miner_reward{block_reward};
    for (const BlockHeader& ommer : evm_.block().ommers) {
        intx::uint256 ommer_reward{((8 + ommer.number - block_number) * block_reward) >> 3};
        state_.add_to_balance(ommer.beneficiary, ommer_reward);
        miner_reward += block_reward / 32;
    }

    state_.add_to_balance(evm_.block().header.beneficiary, miner_reward);
}

}  // namespace silkworm

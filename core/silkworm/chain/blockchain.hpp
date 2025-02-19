/*
   Copyright 2021 The Silkworm Authors

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

#ifndef SILKWORM_CHAIN_BLOCKCHAIN_HPP_
#define SILKWORM_CHAIN_BLOCKCHAIN_HPP_

#include <unordered_map>
#include <vector>

#include <silkworm/chain/validity.hpp>
#include <silkworm/execution/state_pool.hpp>
#include <silkworm/state/state.hpp>
#include <silkworm/types/receipt.hpp>

namespace silkworm {

/// Reference implementation of Ethereum blockchain logic.
/// Used for running consensus tests; the real node will use staged sync instead
/// (https://github.com/ledgerwatch/erigon/blob/devel/eth/stagedsync/README.md)
class Blockchain {
  public:
    /// Creates a new instance of Blockchain.
    /// In the begining the state must have the genesis allocation.
    /// Later on the state may only be modified by the created instance of Blockchain.
    Blockchain(State& state, const ChainConfig& config, const Block& genesis_block);

    // Not copyable nor movable
    Blockchain(const Blockchain&) = delete;
    Blockchain& operator=(const Blockchain&) = delete;

    ValidationResult insert_block(Block& block, bool check_state_root);

    ExecutionStatePool* state_pool{nullptr};

    evmc_vm* exo_evm{nullptr};

  private:
    ValidationResult execute_block(const Block& block, bool check_state_root);

    void re_execute_canonical_chain(uint64_t ancestor, uint64_t tip);

    void unwind_last_changes(uint64_t ancestor, uint64_t tip);

    std::vector<BlockWithHash> intermediate_chain(uint64_t block_number, evmc::bytes32 hash,
                                                  uint64_t canonical_ancestor) const;

    uint64_t canonical_ancestor(const BlockHeader& header, const evmc::bytes32& hash) const;

    State& state_;
    const ChainConfig& config_;
    std::unordered_map<evmc::bytes32, ValidationResult> bad_blocks_;
    std::vector<Receipt> receipts_;
};

}  // namespace silkworm

#endif  // SILKWORM_CHAIN_BLOCKCHAIN_HPP_

/*
   Copyright 2020 - 2021 The Silkworm Authors

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
#include "util.hpp"

#include <memory>
#include <stdexcept>

#include <magic_enum.hpp>

#include <silkworm/db/util.hpp>

namespace silkworm::stagedsync {

void check_stagedsync_error(StageResult code) {
    if (code != StageResult::kSuccess) {
        std::string error{magic_enum::enum_name<StageResult>(code)};
        throw std::runtime_error(error);
    }
}

std::pair<Bytes, Bytes> convert_to_db_format(const ByteView& key, const ByteView& value) {
    if (key.size() == 8) {
        Bytes a(value.data(), kAddressLength);
        Bytes b(value.substr(kAddressLength).data(), value.size() - kAddressLength);
        return {a, b};
    }
    Bytes a(key.substr(8).data(), kAddressLength + db::kIncarnationLength);
    a.append(value.data(), kHashLength);
    Bytes b(value.substr(kHashLength).data(), value.size() - kHashLength);
    return {a, b};
}

/*
 * Truncate a table by starting from a certain key, either up or down.
 * If reverse is set to false, then every key either higher or equal than starting key will be erased.
 * If reverse is set to true, then every key either lower(not equal) than starting key will be erased.
*/
void truncate_table_from(mdbx::cursor& table, Bytes& starting_key, bool reverse) {
    auto current{table.lower_bound(db::to_slice(starting_key), false)};
    if (reverse) {
        current = table.to_previous(false);
    }
    if (current) {
        table.erase();
        while (reverse? table.to_previous(false) : table.to_next(false)) {
            table.erase();
        }
    }
}

}  // namespace silkworm::stagedsync

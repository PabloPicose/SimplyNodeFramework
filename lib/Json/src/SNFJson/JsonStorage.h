#pragma once

#include <nlohmann/json.hpp>

#include <memory>
#include <utility>

namespace snf::detail {

class JsonStorage
{
public:
    explicit JsonStorage(nlohmann::json jsonValue)
        : value(std::move(jsonValue))
    {
    }

    nlohmann::json value;
};

inline std::shared_ptr<JsonStorage> makeJsonStorage(nlohmann::json value)
{
    return std::make_shared<JsonStorage>(std::move(value));
}

}  // namespace snf::detail

#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/database.hpp"
#include "tdur/game/database_resource_key.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string quoted(std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') {
            output << '\\' << static_cast<char>(character);
        } else if (character < 32) {
            output << "\\u00" << std::hex << std::setfill('0') << std::setw(2)
                   << static_cast<unsigned int>(character) << std::dec;
        } else {
            output << static_cast<char>(character);
        }
    }
    output << '"';
    return output.str();
}

int inspect(const std::filesystem::path& game, std::size_t row) {
    const auto key = tdur::game::read_database_asset_key(game);
    if (!key)
        throw std::runtime_error(key.error());
    const auto bank = tdur::assets::BankArchive::read_file(
        game / "Euro" / "Bnk" / "DataBase" / "DB.bnk");
    if (!bank)
        throw std::runtime_error(bank.error().detail);
    const auto entries = bank->entries();
    const auto entry =
        std::find_if(entries.begin(), entries.end(), [](const auto& item) {
            return item.name == "TDU_CarPhysicsData.db";
        });
    if (entry == entries.end())
        throw std::runtime_error("Car physics database is missing");
    const auto bytes =
        bank->stored_bytes(static_cast<std::size_t>(entry - entries.begin()));
    if (!bytes)
        throw std::runtime_error(bytes.error().detail);
    const auto table = tdur::assets::DatabaseTable::parse_asset(*bytes, *key);
    if (!table)
        throw std::runtime_error(table.error().detail);
    // All rows must meet the same named-field contract before reporting a
    // typed drivetrain. Raw storage scales remain explicit in the asset API.
    for (std::size_t index = 0; index < table->rows().size(); ++index) {
        const auto value =
            tdur::assets::read_stored_car_drivetrain(*table, index);
        if (!value)
            throw std::runtime_error(value.error().detail);
    }
    const auto car = tdur::assets::read_stored_car_drivetrain(*table, row);
    if (!car)
        throw std::runtime_error(car.error().detail);
    std::cout << std::setprecision(17)
              << "{\n  \"member\":\"TDU_CarPhysicsData.db\",\n"
              << "  \"topic\":" << quoted(table->topic())
              << ",\n  \"columns\":" << table->columns().size()
              << ",\n  \"validated_drivetrains\":" << table->rows().size()
              << ",\n  \"row\":" << row
              << ",\n  \"reference_token\":" << quoted(car->reference_token)
              << ",\n  \"car_file_resource_token\":"
              << quoted(car->car_file_resource_token)
              << ",\n  \"gearbox_resource_token\":"
              << quoted(car->gearbox_resource_token)
              << ",\n  \"nb_gears\":" << car->nb_gears
              << ",\n  \"final_drive_ratio\":" << car->final_drive_ratio
              << ",\n  \"gear_ratios\":[";
    for (std::size_t gear = 0; gear < car->gear_ratios.size(); ++gear) {
        std::cout << (gear == 0 ? "" : ",") << car->gear_ratios[gear];
    }
    std::cout << "],\n  \"red_line\":" << car->red_line
              << ",\n  \"power_rpm\":" << car->power_rpm
              << ",\n  \"torque_rpm\":" << car->torque_rpm
              << ",\n  \"ignition_rpm\":" << car->ignition_rpm
              << ",\n  \"engine_inertia_raw\":" << car->engine_inertia_raw
              << ",\n  \"gearbox_inertia_raw\":" << car->gearbox_inertia_raw
              << ",\n  \"original_files_modified\":false\n}\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        if ((argc != 3 && argc != 5) ||
            std::wstring_view(argv[1]) != L"--game-dir" ||
            (argc == 5 && std::wstring_view(argv[3]) != L"--row")) {
            std::cerr
                << "Usage: tdur_inspect_database --game-dir PATH [--row N]\n";
            return 2;
        }
        std::size_t row = 0;
        if (argc == 5) {
            std::size_t consumed = 0;
            const std::wstring text(argv[4]);
            const auto parsed = std::stoull(text, &consumed);
            if (consumed != text.size() ||
                parsed > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("Invalid database row index");
            row = static_cast<std::size_t>(parsed);
        }
        return inspect(std::filesystem::path(argv[2]), row);
    } catch (const std::exception& error) {
        std::cerr << "Database inspection: " << error.what() << '\n';
        return 1;
    }
}

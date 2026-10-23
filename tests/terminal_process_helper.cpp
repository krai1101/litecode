#ifdef _WIN32
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::wstring quoteArgument(const std::wstring& value) { return L"\"" + value + L"\""; }

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3)
        return 2;

    const std::wstring mode = argv[1];
    const std::filesystem::path marker(argv[2]);
    if (mode == L"--burst") {
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        const auto writeAll = [output](std::string_view bytes) {
            while (!bytes.empty()) {
                DWORD written = 0;
                if (!WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                               nullptr) ||
                    written == 0)
                    return false;
                bytes.remove_prefix(written);
            }
            return true;
        };
        if (!writeAll("BURST_BEGIN\r\n"))
            return 6;
        std::string chunk;
        chunk.reserve(16 * 1024);
        for (int index = 0; index < 40'000; ++index) {
            const std::string number = std::to_string(index);
            chunk += "ROW" + std::string(5 - number.size(), '0') + number + ':';
            chunk += std::string(32, static_cast<char>('A' + index % 26));
            chunk += "\r\n";
            if (chunk.size() < 16 * 1024)
                continue;
            if (!writeAll(chunk))
                return 6;
            chunk.clear();
        }
        return writeAll(chunk) && writeAll("BURST_COMPLETE\r\n") ? 0 : 7;
    }
    if (mode == L"--child") {
        Sleep(2'000);
        std::ofstream output(marker);
        output << "survived";
        return output ? 0 : 3;
    }
    if (mode != L"--parent")
        return 2;

    std::wstring executable(32'768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || static_cast<size_t>(length) >= executable.size())
        return 4;
    executable.resize(length);

    std::wstring command =
        quoteArgument(executable) + L" --child " + quoteArgument(marker.wstring());
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup, &child)) {
        return 5;
    }
    CloseHandle(child.hThread);
    std::cout << "child-ready" << std::endl;
    WaitForSingleObject(child.hProcess, INFINITE);
    CloseHandle(child.hProcess);
    return 0;
}
#else
int main() { return 0; }
#endif

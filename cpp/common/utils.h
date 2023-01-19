#ifndef __HELLO_WORLD_UTILS_H__
#define __HELLO_WORLD_UTILS_H__

#include <sstream>
#include <iostream>

void ShowHelpAndExit(const char *szBadOption = NULL)
{
    bool bThrowError = false;
    std::ostringstream oss;
    if (szBadOption)
    {
        bThrowError = true;
        oss << "Error parsing \"" << szBadOption << "\"" << std::endl;
    }
    oss << "Options:" << std::endl
        << "    -t / --target: (default: '0.0.0.0:50051') server address." << std::endl
        << "    -t / --mode  : select client/server mode." << std::endl;

    oss << std::endl;

    if (bThrowError)
        throw std::invalid_argument(oss.str());
    else
        std::cout << oss.str();
}

enum class Mode
{
    SERVER,
    CLIENT,
};

enum class ParseCLIState
{
    SUCCESS,
    ERROR,
    SHOW_HELP,
};

ParseCLIState ParseCommandLine(int argc, char *argv[], std::string &server_address, Mode &mode)
{
    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == std::string("--help") || std::string(argv[i]) == std::string("-h"))
        {
            ShowHelpAndExit();
            return ParseCLIState::SHOW_HELP;
        }
        else if (std::string(argv[i]) == std::string("--target") || std::string(argv[i]) == std::string("-t"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--target");
                return ParseCLIState::ERROR;
            }
            else
                server_address = std::string(argv[i]);
            continue;
        }
        else if (std::string(argv[i]) == std::string("--mode") || std::string(argv[i]) == std::string("-m"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--mode");
                return ParseCLIState::ERROR;
            }
            else if (argv[i] == "server")
                mode = Mode::SERVER;
            else
                mode = Mode::CLIENT;
            continue;
        }
        else if (std::string(argv[i]) == std::string("-s"))
        {
            mode = Mode::SERVER;
            continue;
        }
        else if (std::string(argv[i]) == std::string("-c"))
        {
            mode = Mode::CLIENT;
            continue;
        }
        else
        {
            {
                ShowHelpAndExit((std::string("input not include ") + std::string(argv[i])).c_str());
                return ParseCLIState::ERROR;
            }
        }
    }
    return ParseCLIState::SUCCESS;
}

#endif // __HELLO_WORLD_UTILS_H__
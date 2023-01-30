#ifndef __HELLO_WORLD_UTILS_H__
#define __HELLO_WORLD_UTILS_H__

#include <cassert>
#include <iostream>
#include <sstream>

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
        << "    -t / --server_address / --target: (default: '0.0.0.0:50051') server address." << std::endl
        << "    --server_ip: (default: '0.0.0.0') server IP." << std::endl
        << "    --server_port: (default: 50051) server port." << std::endl
        << "    --maintenance_address: (default: '0.0.0.0:50052') maintenance address." << std::endl
        << "    --maintenance_ip: (default: '0.0.0.0') maintenance IP." << std::endl
        << "    --maintenance_port: (default: 50052) maintenance port." << std::endl
        << "    --secure: (default: false) secure mode." << std::endl
        << "    -s / -c / --mode [\"client\"/\"server\"] : select client/server mode." << std::endl
        << "    -db / --database : path to Database." << std::endl;

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

typedef struct CliParams_
{
    CliParams_()
        : server_address{"0.0.0.0:50051"}, server_ip{"0.0.0.0"}, server_port{50051},
          maintenance_address{"0.0.0.0:50052"}, maintenance_ip{"0.0.0.0"},
          maintenance_port{50052}, mode{Mode::CLIENT} {};
    std::string server_address;
    bool server_address_enabled = false;
    std::string server_ip;
    bool server_ip_enabled = false;
    int server_port;
    bool server_port_enabled = false;
    std::string maintenance_address;
    bool maintenance_address_enabled = false;
    std::string maintenance_ip;
    bool maintenance_ip_enabled = false;
    int maintenance_port;
    bool maintenance_port_enabled = false;
    Mode mode;
    bool mode_enabled = false;
    bool secure = false;
    std::string database;
    bool database_enabled = false;
} CliParams;

ParseCLIState ParseCommandLine(int argc, char *argv[], CliParams *cliParams)
{
    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == std::string("--help") || std::string(argv[i]) == std::string("-h"))
        {
            ShowHelpAndExit();
            return ParseCLIState::SHOW_HELP;
        }
        else if (std::string(argv[i]) == std::string("--target") || std::string(argv[i]) == std::string("-t") ||
                 std::string(argv[i]) == std::string("--server_address"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--target");
                return ParseCLIState::ERROR;
            }
            else
            {
                cliParams->server_address = std::string(argv[i]);
                cliParams->server_address = true;
            }
            continue;
        }
        else if (std::string(argv[i]) == std::string("--server_ip"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--server_ip");
                return ParseCLIState::ERROR;
            }
            else
            {
                cliParams->server_ip = std::string(argv[i]);
                cliParams->server_ip_enabled = true;
            }
            continue;
        }
        else if (std::string(argv[i]) == std::string("--server_port"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--server_port");
                return ParseCLIState::ERROR;
            }
            else
            {
                cliParams->server_port = std::atoi(argv[i]);
                cliParams->server_port_enabled = true;
            }
            continue;
        }
        else if (std::string(argv[i]) == std::string("--maintenance_address"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--maintenance_address");
                return ParseCLIState::ERROR;
            }
            else
            {
                cliParams->maintenance_address = std::string(argv[i]);
                cliParams->maintenance_address_enabled = true;
            }
            continue;
        }
        else if (std::string(argv[i]) == std::string("--maintenance_ip"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--maintenance_ip");
                return ParseCLIState::ERROR;
            }
            else
            {
                cliParams->maintenance_ip = std::string(argv[i]);
                cliParams->maintenance_ip_enabled = true;
            }
            continue;
        }
        else if (std::string(argv[i]) == std::string("--maintenance_port"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--maintenance_port");
                return ParseCLIState::ERROR;
            }
            else
            {
                cliParams->maintenance_port = std::atoi(argv[i]);
                cliParams->maintenance_port_enabled = true;
            }
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
                cliParams->mode = Mode::SERVER;
            else
            {
                assert(("--mode must be one of client or server.", argv[i] == "client"));
                cliParams->mode = Mode::CLIENT;
            }
            cliParams->mode_enabled = true;
            continue;
        }
        else if (std::string(argv[i]) == std::string("-s"))
        {
            cliParams->mode = Mode::SERVER;
            cliParams->mode_enabled = true;
            continue;
        }
        else if (std::string(argv[i]) == std::string("-c"))
        {
            cliParams->mode = Mode::CLIENT;
            cliParams->mode_enabled = true;
            continue;
        }
        else if (std::string(argv[i]) == std::string("--secure"))
        {
            cliParams->secure = true;
            continue;
        }
        else if (std::string(argv[i]) == std::string("--database") || std::string(argv[i]) == std::string("-db"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit("--database");
                return ParseCLIState::ERROR;
            }
            else
            {
                cliParams->database = std::string(argv[i]);
                cliParams->database_enabled = true;
            }
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

    if (cliParams->maintenance_address_enabled)
        ;
    else if (cliParams->maintenance_port_enabled || cliParams->maintenance_ip_enabled)
        cliParams->maintenance_address =
            cliParams->maintenance_ip + std::string(":") + std::to_string(cliParams->maintenance_port);

    if (cliParams->server_address_enabled)
        ;
    else if (cliParams->server_port_enabled || cliParams->server_ip_enabled)
        cliParams->server_address = cliParams->server_ip + std::string(":") + std::to_string(cliParams->server_port);
    return ParseCLIState::SUCCESS;
}

#endif // __HELLO_WORLD_UTILS_H__
#include "Application.h"
#include "core/CommandLine.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

int wmain(int argc, wchar_t* argv[])
{
    try
    {
        std::vector<std::wstring_view> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index)
        {
            arguments.emplace_back(argv[index]);
        }

        const daedalus::CommandLineOptions options = daedalus::parse_command_line(arguments);
        if (options.show_help)
        {
            std::cout << daedalus::usage_text();
            return EXIT_SUCCESS;
        }
        return daedalus::Application::execute(options);
    }
    catch (const daedalus::CommandLineError& error)
    {
        std::cerr << "Argument error: " << error.what() << "\n\n" << daedalus::usage_text();
        return EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Fatal startup error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

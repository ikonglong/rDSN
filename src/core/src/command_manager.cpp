/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 * 
 * -=- Robust Distributed System Nucleus (rDSN) -=- 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Description:
 *     What is this file about?
 *
 * Revision history:
 *     xxxx-xx-xx, author, first version
 *     xxxx-xx-xx, author, fix bug about xxx
 */

# include "command_manager.h"
# include <iostream>
# include <thread>
# include <sstream>
# include <dsn/cpp/utils.h>
# include <dsn/cpp/rpc_stream.h>
# include "service_engine.h"
# include <dsn/tool-api/task.h>
# include <dsn/tool-api/rpc_message.h>
# include "rpc_engine.h"
# include <dsn/cpp/cli.h>

# ifdef __TITLE__
# undef __TITLE__
# endif
# define __TITLE__ "command_manager"

DSN_API const char* dsn_cli_run(const char* command_line) // return command output
{
    ::dsn::safe_string cmd = command_line;
    ::dsn::safe_string output;
    dsn::command_manager::instance().run_command(cmd, output);

    char* c_output = (char*)malloc(output.length() + 1);
    memcpy(c_output, &output[0], output.length());
    c_output[output.length()] = '\0';
    return c_output;
}

DSN_API void dsn_cli_free(const char* command_output)
{
    ::free((void*)command_output);
}

DSN_API dsn_handle_t dsn_cli_register(
    const char* command,
    const char* help_one_line,
    const char* help_long,
    void* context,
    dsn_cli_handler cmd_handler,
    dsn_cli_free_handler output_freer
    )
{
    return dsn::register_command(
        command,
        help_one_line,
        help_long,
        [=](const ::dsn::safe_vector< ::dsn::safe_string>& args)
        {
            std::vector<const char*> c_args;
            for (auto& s : args)
            {
                c_args.push_back(s.c_str());
            }
            dsn_cli_reply reply;
            cmd_handler(context, (int)c_args.size(), c_args.empty() ? nullptr : (const char**)&c_args[0], &reply);
            auto cpp_output = dsn::safe_string(reply.message, reply.message + reply.size);
            output_freer(reply);
            return cpp_output;
        }
        );
}

DSN_API dsn_handle_t dsn_cli_app_register(
    const char* command,        //registered command, you should call this command by app_full_name.command
    const char* help_one_line,
    const char* help_long,
    void* context,
    dsn_cli_handler cmd_handler,
    dsn_cli_free_handler output_freer
    )
{ 
    auto cnode = ::dsn::task::get_current_node2();
    dassert(cnode != nullptr, "tls_dsn not inited properly");
    auto handle = dsn_cli_register(
        (std::string(cnode->name()) + "." + command).c_str(),
        (std::string(cnode->name()) + "." + command + " " + help_one_line).c_str(),
        help_long,
        context,
        cmd_handler,
        output_freer
        );
    dsn::command_manager::instance().set_cli_target_address(handle, dsn::task::get_current_rpc()->primary_address());
    return handle;
}

DSN_API void dsn_cli_deregister(dsn_handle_t handle)
{
    dsn::deregister_command(handle);
}

namespace dsn {

    void deregister_command(dsn_handle_t command_handle)
    {
        return command_manager::instance().deregister_command(command_handle);
    }

    dsn_handle_t register_command(
        const safe_vector<const char*>& commands, // commands, e.g., {"help", "Help", "HELP", "h", "H"}
        const char* help_one_line,
        const char* help_long, // help info for users
        command_handler handler
        )
    {
        return command_manager::instance().register_command(commands, help_one_line, help_long, handler);
    }

    dsn_handle_t register_command(
        const char* command, // commands, e.g., "help"
        const char* help_one_line,
        const char* help_long,
        command_handler handler
        )
    {
        safe_vector<const char*> cmds;
        cmds.push_back(command);
        return register_command(cmds, help_one_line, help_long, handler);
    }

    bool run_command(const char* cmd, /* out */ ::dsn::safe_string& output)
    {
        return command_manager::instance().run_command(cmd, output);
    }

    dsn_handle_t command_manager::register_command(const safe_vector<const char*>& commands, const char* help_one_line, const char* help_long, command_handler handler)
    {
        utils::auto_write_lock l(_lock);

        for (auto cmd : commands)
        {
            if (cmd != nullptr)
            {
                auto it = _handlers.find(cmd);
                dassert(it == _handlers.end(), "command '%s' already regisered", cmd);
            }
        }

        command* c = new command;
        c->address.set_invalid();
        c->commands = commands;
        c->help_long = help_long;
        c->help_short = help_one_line;
        c->handler = handler;
        _commands.push_back(c);
        
        for (auto cmd : commands)
        {
            if (cmd != nullptr)
            {
                _handlers[cmd] = c;
            }
        }
        return c;
    }
    
    void command_manager::deregister_command(dsn_handle_t handle)
    {
        auto c = reinterpret_cast<command*>(handle);
        dassert(c != nullptr, "cannot deregister a null handle");
        utils::auto_write_lock l(_lock);
        for (auto cmd : c->commands)
        {
            if (cmd != nullptr)
            {
                auto it = _handlers.find(cmd);
                if (it != _handlers.end())
                {
                    _handlers.erase(it);
                }
            }
        }
        delete c;

    }

    bool command_manager::run_command(const safe_string& cmdline, /*out*/ safe_string& output)
    {
        auto cnode = ::dsn::task::get_current_node2();
        if (cnode == nullptr)
        {
            auto& all_nodes = ::dsn::service_engine::fast_instance().get_all_nodes();
            if (all_nodes.empty())
            {
                derror("no apps are started, no way to mimic");
                return false;
            }
            dsn_mimic_app(all_nodes.begin()->second->spec().role_name.c_str(), 1);
        }

        auto scmd = cmdline;
        safe_vector<safe_string> args;
        
        utils::split_args(scmd.c_str(), args, ' ');

        if (args.size() < 1)
            return false;

        safe_vector<safe_string> args2;
        for (size_t i = 1; i < args.size(); i++)
        {
            args2.push_back(args[i]);
        }

        return run_command(args[0], args2, output);
    }

    bool command_manager::run_command(const safe_string& cmd, const safe_vector<safe_string>& args, /*out*/ safe_string& output)
    {
        command* h = nullptr;
        {
            utils::auto_read_lock l(_lock);
            auto it = _handlers.find(cmd);
            if (it != _handlers.end())
                h = it->second;
        }

        if (h == nullptr)
        {
            output = safe_string("unknown command '") + cmd + "'";
            return false;
        }
        else
        {
            if (h->address.is_invalid() || h->address == dsn::task::get_current_rpc()->primary_address())
            {
                output = h->handler(args);
                return true;
            }
            else
            {
                ::dsn::rpc_read_stream response;
                
                dsn_message_t msg = dsn_msg_create_request(RPC_CLI_CLI_CALL);
                ::dsn::command rcmd;
                rcmd.cmd = cmd.c_str();
                for (auto& e : args)
                {
                    rcmd.arguments.emplace_back(e.c_str());
                }

                ::dsn::marshall(msg, rcmd);
                auto resp = dsn_rpc_call_wait(h->address.c_addr(), msg);
                if (resp != nullptr)
                {
                    std::string o2 = output.c_str();
                    ::dsn::unmarshall(resp, o2);
                    return true;
                }
                else
                {
                    dwarn("cli run for %s is too long, timeout", cmd.c_str());
                    return false;
                }
            }
        }
    }

    void command_manager::run_console()
    {
        std::cout << "dsn cli begin ... (type 'help' + Enter to learn more)" << std::endl;
        std::cout << ">";

        std::string cmdline;
        while (std::getline(std::cin, cmdline))
        {
            safe_string result;
            run_command(cmdline.c_str(), result);
            std::cout << result << std::endl;
            std::cout << ">";
        }
    }

    void command_manager::start_local_cli()
    {
        new std::thread(std::bind(&command_manager::run_console, this));
    }

    void remote_cli_handler(dsn_message_t req, void*)
    {
        command_manager::instance().on_remote_cli(req);
    }

    void command_manager::start_remote_cli()
    {
        ::dsn::service_engine::fast_instance().register_system_rpc_handler(RPC_CLI_CLI_CALL, "dsn.cli", remote_cli_handler, nullptr);
    }

    void command_manager::on_remote_cli(dsn_message_t req)
    {
        ::dsn::command cmd;
        safe_string result;

        ::dsn::unmarshall(req, cmd);

        safe_vector<safe_string> args;
        for (auto& e : cmd.arguments)
        {
            args.emplace_back(e.c_str());
        }

        run_command(cmd.cmd.c_str(), args, result);

        auto resp = dsn_msg_create_response(req);

        std::string r2 = result.c_str();
        ::dsn::marshall(resp, r2);
        dsn_rpc_reply(resp);
    }

    void command_manager::set_cli_target_address(dsn_handle_t handle, dsn::rpc_address address)
    {
        reinterpret_cast<command*>(handle)->address = address;
    }

    command_manager::command_manager()
    {
        register_command(
            {"help", "h", "H", "Help"}, 
            "help|Help|h|H [command] - display help information", 
            "",
            [this](const safe_vector<safe_string>& args)
            {
                safe_sstream ss;

                if (args.size() == 0)
                {
                    utils::auto_read_lock l(_lock);
                    for (auto c : this->_commands)
                    {
                        ss << c->help_short << std::endl;
                    }
                }
                else
                {
                    utils::auto_read_lock l(_lock);
                    auto it = _handlers.find(args[0]);
                    if (it == _handlers.end())
                        ss << "cannot find command '" << args[0] << "'";
                    else
                    {
                        ss.width(6);
                        ss << std::left << it->first << ": " << it->second->help_short << std::endl << it->second->help_long << std::endl;
                    }
                }

                return ss.str();
            }
        );

        register_command(
            { "repeat", "r", "R", "Repeat" },
            "repeat|Repeat|r|R interval_seconds max_count command - execute command periodically",
            "repeat|Repeat|r|R interval_seconds max_count command - execute command every interval seconds, to the max count as max_count (0 for infinite)",
            [this](const safe_vector<safe_string>& args)
        {
            safe_sstream ss;

            if (args.size() < 3)
            {
                return "insufficient arguments";
            }

            int interval_seconds = atoi(args[0].c_str());
            if (interval_seconds <= 0)
            {
                return "invalid interval argument";
            }

            int max_count = atoi(args[1].c_str());
            if (max_count < 0)
            {
                return "invalid max count";
            }

            if (max_count == 0)
            {
                max_count = std::numeric_limits<int>::max();
            }

            safe_string cmd = args[2];
            safe_vector<safe_string> largs;
            for (int i = 3; i < (int)args.size(); i++)
            {
                largs.push_back(args[i]);
            }

            for (int i = 0; i < max_count; i++)
            {
                safe_string output;
                auto r = this->run_command(cmd, largs, output);

                if (!r)
                {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
            }

            return "repeat command completed";
        }
        );
    }
}

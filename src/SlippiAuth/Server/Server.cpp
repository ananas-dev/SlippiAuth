#include "Server.h"

namespace SlippiAuth
{
    Server::Server(uint16_t port) : m_Port(port)
    {
        m_Server.init_asio();

        // Handlers
        m_Server.set_open_handler([this](auto&& hdl)
        {
            return OnOpen(std::forward<decltype(hdl)>(hdl));
        });

        m_Server.set_message_handler([this](auto&& hdl, auto&& msg)
        {
            return OnMessage(std::forward<decltype(hdl)>(hdl), std::forward<decltype(msg)>(msg));
        });

        m_Server.set_fail_handler([this](auto&& hdl)
        {
            return OnFail(std::forward<decltype(hdl)>(hdl));
        });

        m_Server.set_close_handler([this](auto&& hdl)
        {
            return OnClose(std::forward<decltype(hdl)>(hdl));
        });

        // Remove address-in-use exception when restarting
        m_Server.set_reuse_addr(true);

        m_Server.listen(port);

        // Set logger
        m_Server.clear_access_channels(websocketpp::log::elevel::rerror);
    }

    void Server::OnEvent(Event& e)
    {
        CORE_TRACE(e);

        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<SearchingEvent>(BIND_EVENT_FN(Server::OnClientSpawn));
        dispatcher.Dispatch<AuthenticatedEvent>(BIND_EVENT_FN(Server::OnAuthenticated));
        dispatcher.Dispatch<SlippiErrorEvent>(BIND_EVENT_FN(Server::OnSlippiError));
        dispatcher.Dispatch<TimeoutEvent>(BIND_EVENT_FN(Server::OnTimeout));
        dispatcher.Dispatch<NoReadyClientEvent>(BIND_EVENT_FN(Server::OnNoReadyClient));
    }

    bool Server::OnClientSpawn(SearchingEvent& e)
    {
        Json message = {
                {"type", "searching"},
                {"discordId", e.GetDiscordId()},
                {"botCode", e.GetBotConnectCode()},
                {"userCode", e.GetUserConnectCode()}
        };

        SendMessage(message);
        return true;
    }

    bool Server::OnAuthenticated(AuthenticatedEvent& e)
    {
        Json message = {
                {"type", "authenticated"},
                {"discordId", e.GetDiscordId()},
                {"userCode", e.GetUserConnectCode()},
                {"userName", e.GetUserName()},
                {"userIp", e.GetUserIp()}
        };

        SendMessage(message);
        return true;
    }

    bool Server::OnSlippiError(SlippiErrorEvent& e)
    {
        Json message = {
                {"type", "slippiErr"},
                {"discordId", e.GetDiscordId()},
                {"userCode", e.GetUserConnectCode()}
        };

        SendMessage(message);
        return true;
    }

    bool Server::OnTimeout(TimeoutEvent& e)
    {
        Json message = {
                {"type", "timeout"},
                {"discordId", e.GetDiscordId()},
                {"userCode", e.GetUserConnectCode()}
        };

        SendMessage(message);
        return true;
    }

    bool Server::OnNoReadyClient(NoReadyClientEvent& e)
    {
        Json message = {
                {"type", "noReadyClient"},
                {"discordId", e.GetDiscordId()},
                {"userCode", e.GetUserConnectCode()}
        };

        SendMessage(message);
        return true;
    }

    void Server::OnOpen(const websocketpp::connection_hdl& hdl)
    {
        SERVER_INFO("A websocket client connected");
        m_ConnectionHandles.push_back(hdl);
    }

    void Server::OnMessage(const websocketpp::connection_hdl& hdl, const MessagePtr& msg)
    {
        try
        {
            // Deserialize json
            try
            {
                auto payload = msg->get_payload();
                if (payload == "ping") {
                    m_Server.send(hdl, "pong", msg->get_opcode());
                } else {
                    Json message = Json::parse(payload);
                    SERVER_TRACE("Received {} message", message["type"]);
                    if (message["type"] == "queue")
                    {
                        if (message.contains("userCode") && message.contains("timeout") && message.contains("discordId"))
                        {
                            QueueEvent event(message["userCode"], message["timeout"], message["discordId"]);
                            m_EventCallback(event);
                        }
                        else
                        {
                            OnMissingArg(hdl, "code, timeout or discordId");
                        }
                    }
                    else if (message["type"] == "stopListening")
                    {
                        m_Server.stop_listening();
                    }
                    else
                    {
                        Json response = {{"type", "unknownCommand"}};
                        m_Server.send(hdl, response.dump(), msg->get_opcode());
                    }
                }
            }
            catch (const nlohmann::detail::parse_error& e)
            {
                Json response = {{"type", "jsonErr"}};
                m_Server.send(hdl, response.dump(), msg->get_opcode());
            }
        }
        catch (const websocketpp::exception& e)
        {
            SERVER_ERROR("Failed to send message: {}", e.what());
        }
    }

    void Server::OnFail(const websocketpp::connection_hdl& hdl)
    {
        WsServer::connection_ptr con = m_Server.get_con_from_hdl(hdl);
        SERVER_ERROR("{} {}", con->get_ec(), con->get_ec().message());
    }

    void Server::OnClose(const websocketpp::connection_hdl& hdl)
    {
        SERVER_INFO("A websocket client disconnected");

        auto iter = std::find_if(m_ConnectionHandles.begin(), m_ConnectionHandles.end(),
                [=](const websocketpp::connection_hdl& hdl2)
                {
                    return (m_Server.get_con_from_hdl(hdl) == m_Server.get_con_from_hdl(hdl2));
                });

        if (iter != m_ConnectionHandles.end())
        {
            m_ConnectionHandles.erase(iter);
        }
    }

    void Server::Start()
    {
        SERVER_INFO("Server started on port {}", m_Port);
        m_Server.start_accept();
        m_Server.run();
    }

    void Server::SendMessage(const Json& message)
    {
        try
        {
            for (auto& hdl : m_ConnectionHandles)
            {
                if (!hdl.expired())
                {
                    m_Server.send(hdl, message.dump(), websocketpp::frame::opcode::text);
                }
            }
        }
        catch (const websocketpp::exception& e)
        {
            SERVER_ERROR("Failed to send message: {}", e.what());
        }
    }

    void Server::SendMessage(const websocketpp::connection_hdl& hdl, const Json& message)
    {
        try
        {
            m_Server.send(hdl, message.dump(), websocketpp::frame::opcode::text);
        }
        catch (const websocketpp::exception& e)
        {
            SERVER_ERROR("Failed to send message: {}", e.what());
        }
    }

    void Server::OnMissingArg(const websocketpp::connection_hdl& hdl, const std::string& argName)
    {
        Json message = {
                {"type", "missingArg"},
                {"what", argName}
        };

        SendMessage(hdl, message);
    }

}

#pragma once

#include "SlippiAuth/Client/Client.h"
#include "SlippiAuth/Events/ServerEvent.h"
#include "SlippiAuth/Core.h"

namespace SlippiAuth {

    class ClientPool
    {
    public:
        explicit ClientPool();
        ~ClientPool();

        void OnEvent(Event& e);
        bool OnQueue(QueueEvent& e);

        int64_t FindReadyClientIndex();

        void StartClient(const std::string& connectCode, uint32_t timeout, uint64_t discordId);

        std::vector<Client>& GetClients()
        {
            return m_Clients;
        }

        inline void SetEventCallback(const EventCallbackFn& callback)
        {
            m_EventCallback = callback;
        }
    private:
        void RemoveThread(std::thread::id id);
    private:
        uint32_t m_PoolSize = ClientConfig::Get().size();
        std::vector<Client> m_Clients;
        std::vector<std::thread> m_Threads;
        std::mutex m_ThreadMutex;

        EventCallbackFn m_EventCallback;
    };

}

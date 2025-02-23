#include "ABXTickerClient.h"

void CABXTickerClient::HandleSocketError(const std::string& message) {
    Log(message + std::to_string(WSAGetLastError()));
    WSACleanup();
    exit(EXIT_FAILURE);
}

void CABXTickerClient::SendStreamAllPacketsRequest()
{
    char lcPayLoad[3] = { 0 };
    RequestPayload lstPayLoad = { 0 };
    lstPayLoad.CallType = 1;
    lstPayLoad.ResendRequest = 0;
    memcpy(lcPayLoad, &lstPayLoad, 2);
    int bytesSent = send(m_nClientSocket, lcPayLoad, sizeof(lstPayLoad), 0);
    if (bytesSent == SOCKET_ERROR) {
        HandleSocketError("Failed to send stream all packets request :: SendStreamAllPacketsRequest()");
    }
    Log("Request for Stream All packets sent :: SendStreamAllPacketsRequest()");
    ReceiveStreamAllPacketsResponse();
}
void CABXTickerClient::GetMissingTickerData(uint8_t cSeqNo)
{
    char lcMissingPacketReq[3] = { 0 };
    char lcRecvBuffer[20] = { 0 };
    const int packetSize = 17;
    RequestPayload lstReqPayLoad = { 0 };
    lstReqPayLoad.CallType = 2;
    lstReqPayLoad.ResendRequest = cSeqNo;
    memcpy(lcMissingPacketReq, &lstReqPayLoad, 2);
    int lnBytesSent = send(m_nClientSocket, lcMissingPacketReq, sizeof(lstReqPayLoad), 0);
    if (lnBytesSent == SOCKET_ERROR) {
        HandleSocketError("Failed to send missing packets request :: GetMissingTickerData()");
    }
    Log("Requested for missing packet with sequence no. : " + std::to_string(cSeqNo));
    int bytesReceived = recv(m_nClientSocket, lcRecvBuffer, packetSize, 0);
    if (bytesReceived == SOCKET_ERROR)
    {
        HandleSocketError("Failed to receive response :: GetMissingTickerData()");
    }
    else if (bytesReceived < packetSize)
    {
        Log("Incomplete packet received :: GetMissingTickerData()");
    }
    std::unique_ptr<ResponsePayload> luptrResponsePayLoad(new ResponsePayload);
    luptrResponsePayLoad->BuyOrSell = ((ResponsePayload*)lcRecvBuffer)->BuyOrSell;
    luptrResponsePayLoad->Quantity = ntohl(*reinterpret_cast<int*>(&lcRecvBuffer[5]));
    luptrResponsePayLoad->Price = ntohl(*reinterpret_cast<int*>(&lcRecvBuffer[9]));
    luptrResponsePayLoad->Sequence = ntohl(*reinterpret_cast<int*>(&lcRecvBuffer[13]));
    memcpy(luptrResponsePayLoad->Symbol, ((ResponsePayload*)lcRecvBuffer)->Symbol, 4);
    m_mapResponses[luptrResponsePayLoad->Sequence] = std::move(luptrResponsePayLoad);
}
void CABXTickerClient::ReceiveStreamAllPacketsResponse() {
    const int packetSize = 17;
    char lcRecvBuffer[20] = { 0 };
    int PrevSequence = 0;
    while (true) {
        memset(lcRecvBuffer, 0, sizeof(lcRecvBuffer));
        int bytesReceived = recv(m_nClientSocket, lcRecvBuffer, packetSize, 0);
        if (bytesReceived == 0) {
            closesocket(m_nClientSocket);
            WSACleanup();
            m_nLastSequence = PrevSequence;
            Log("Server closed the connection :: ReceiveStreamAllPacketsResponse()");
            if (!m_setMissingSequences.empty())
            {
                CreateSocket();
                for (uint8_t lnSeq : m_setMissingSequences)
                {
                    GetMissingTickerData(lnSeq);
                }
                m_setMissingSequences.clear();
            }
            break;
        }
        else if (bytesReceived == SOCKET_ERROR) {
            HandleSocketError("Failed to receive response :: ReceiveStreamAllPacketsResponse()");
        }

        if (bytesReceived < packetSize) {
            Log("Incomplete packet received :: ReceiveStreamAllPacketsResponse()");
            continue;
        }
        std::unique_ptr<ResponsePayload> luptrResponsePayLoad(new ResponsePayload);
        luptrResponsePayLoad->BuyOrSell = ((ResponsePayload*)lcRecvBuffer)->BuyOrSell;
        luptrResponsePayLoad->Quantity = ntohl(*reinterpret_cast<int*>(&lcRecvBuffer[5]));
        luptrResponsePayLoad->Price = ntohl(*reinterpret_cast<int*>(&lcRecvBuffer[9]));
        luptrResponsePayLoad->Sequence = ntohl(*reinterpret_cast<int*>(&lcRecvBuffer[13]));
        memcpy(luptrResponsePayLoad->Symbol, ((ResponsePayload*)lcRecvBuffer)->Symbol, 4);
        m_nFirstSequence = min(luptrResponsePayLoad->Sequence, m_nFirstSequence);
        while ((PrevSequence + 1) < luptrResponsePayLoad->Sequence)
        {
            m_nFirstSequence = min(PrevSequence + 1, m_nFirstSequence);
            m_setMissingSequences.insert(PrevSequence + 1);
            PrevSequence++;
        }
        PrevSequence = luptrResponsePayLoad->Sequence;
        m_mapResponses[luptrResponsePayLoad->Sequence] = std::move(luptrResponsePayLoad);
    }
}
void CABXTickerClient::CreateSocket()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        HandleSocketError("WSAStartup failed :: CreateSocket()");
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        HandleSocketError("Socket creation failed :: CreateSocket()");
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        HandleSocketError("Failed to connect to server :: CreateSocket()");
    }
    Log("Connected to server :: CreateSocket()");
    m_nClientSocket = clientSocket;
}
void CABXTickerClient::AppendTickerToJSON()
{
    ResponsePayload* lstResponse = nullptr;
    json responseArray = json::array();
    for (int i = m_nFirstSequence; i <= m_nLastSequence; i++)
    {
        lstResponse = m_mapResponses[i].get();
        char lcSymbol[5] = { 0 };
        char lcBuyOrSymbol[2] = { 0 };
        memcpy(lcSymbol, lstResponse->Symbol, 4);
        json packet = {
            {"symbol", std::string(lcSymbol)},
            {"buy_sell", std::string(1,lstResponse->BuyOrSell)},
            {"quantity", lstResponse->Quantity},
            {"price", lstResponse->Price},
            {"sequence", lstResponse->Sequence}
        };
        responseArray.push_back(packet);
        lstResponse = nullptr;
    }
    std::ofstream file("ABXTickerData.json", std::ios::app);
    try {
        file << responseArray.dump(4);
        Log("Ticker data written to file successfully :: AppendTickerToJSON()\n");
        std::cout << "Ticker data written to file successfully" << std::endl;
    }
    catch (const std::exception& e) {
        std::string lstrException = e.what();
        Log("Exception " + lstrException + ":: AppendTickerToJSON()");
    }
    file.close();
}
void CABXTickerClient::Log(const std::string& message)
{
    {
        std::lock_guard<std::mutex> lock(m_mLoggingMutex);
        m_qLoggingQueue.push(message);
    }
    m_cLogEvent.notify_one();
}
void CABXTickerClient::ProcessLogs()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_mLoggingMutex);
        m_cLogEvent.wait(lock, [this] {return !m_qLoggingQueue.empty(); });

        while (!m_qLoggingQueue.empty())
        {
            std::string lstrMessageLog = m_qLoggingQueue.front();
            m_qLoggingQueue.pop();
            m_mLoggingMutex.unlock();

            auto now = std::chrono::system_clock::now();
            std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
            if (m_logFile.is_open()) {
                m_logFile << std::put_time(std::localtime(&currentTime), "%Y-%m-%d %H:%M:%S - ") << lstrMessageLog << std::endl;
            }
            m_mLoggingMutex.lock();
        }
    }
}
int main() {
    CABXTickerClient lobjABXTickerClient;
    while (1)
    {
        std::cout << "1. Stream all packets" << std::endl;
        std::cout << "2. Exit" << std::endl;
        int choice;
        std::cin >> choice;
        switch (choice)
        {
        case 1:
            lobjABXTickerClient.CreateSocket();
            lobjABXTickerClient.SendStreamAllPacketsRequest();
            lobjABXTickerClient.AppendTickerToJSON();
            break;

        case 2:
            return 0;
        }
    }

    return 0;
}

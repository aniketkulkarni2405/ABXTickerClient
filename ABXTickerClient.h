#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <queue>
#include "json.hpp"
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include <unordered_set>

#pragma comment(lib, "Ws2_32.lib")
#define LOG_FILE "ABXLogs"
#define SERVER_PORT 3000
#define SERVER_IP "127.0.0.1"
using json = nlohmann::json;

struct RequestPayload
{
	uint8_t CallType;
	uint8_t ResendRequest;
};

struct ResponsePayload
{
	char Symbol[4];
	char BuyOrSell;
	int Quantity;
	int Price;
	int Sequence;
};

class CABXTickerClient
{
private:
	SOCKET m_nClientSocket;
	std::unordered_set<uint8_t> m_setMissingSequences;
	std::unordered_map<int, std::unique_ptr<ResponsePayload>> m_mapResponses;
	int m_nLastSequence;
	int m_nFirstSequence = INT_MAX;
	std::thread m_nLoggingThread;
	std::queue<std::string> m_qLoggingQueue;
	std::mutex m_mLoggingMutex;
	std::condition_variable m_cLogEvent;
	std::ofstream m_logFile;
public:
	CABXTickerClient() :m_logFile(LOG_FILE, std::ios::app)
	{
		if (!m_logFile.is_open())
		{
			std::cout << "Log file not opened. No logs will be generated." << std::endl;
		}
		else
		{
			m_logFile << "=========== ABX TICKER CLIENT ===========" << std::endl;
			m_nLoggingThread = std::thread(&CABXTickerClient::ProcessLogs, this);
			m_nLoggingThread.detach();
		}
	}
	~CABXTickerClient()
	{
		closesocket(m_nClientSocket);
		WSACleanup();
		if (m_logFile.is_open()) {
			m_logFile.close();
		}
	}
	void CreateSocket();
	void SendStreamAllPacketsRequest();
	void GetMissingTickerData(unsigned char cSeqNo);
	void ReceiveStreamAllPacketsResponse();
	void HandleSocketError(const std::string& message);
	void AppendTickerToJSON();
	void ProcessLogs();
	void Log(const std::string& message);
};
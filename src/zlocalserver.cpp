#include "zlocalserver.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <cwchar>
#elif !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <algorithm>
extern char **environ;
#endif
#include <utility>
#include <chrono>
#include <algorithm>
using namespace std;

ZLocalServerProcess::~ZLocalServerProcess() { Stop(); }
ZLocalServerProcess::ZLocalServerProcess(ZLocalServerProcess &&other) noexcept
    : process(other.process), job(other.job) { other.process = other.job = 0; }
ZLocalServerProcess &ZLocalServerProcess::operator=(ZLocalServerProcess &&other) noexcept
{
    if(this != &other) { Stop(); process = other.process; job = other.job; other.process = other.job = 0; }
    return *this;
}
bool ZLocalServerProcess::Running()
{
    if(!process) return false;
#ifdef _WIN32
    if(WaitForSingleObject((HANDLE)process, 0) == WAIT_TIMEOUT) return true;
    CloseHandle((HANDLE)process);
    if(job) CloseHandle((HANDLE)job);
#elif !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    int status;
    pid_t result;
    do { result = waitpid((pid_t)process, &status, WNOHANG); } while(result < 0 && errno == EINTR);
    if(result == 0) return true;
#endif
    process = job = 0;
    return false;
}
void ZLocalServerProcess::Stop()
{
    if(!process) return;
#ifdef _WIN32
    if(job) CloseHandle((HANDLE)job); //KILL_ON_JOB_CLOSE also handles parent crashes
    else TerminateProcess((HANDLE)process, 1);
    WaitForSingleObject((HANDLE)process, 5000);
    CloseHandle((HANDLE)process);
#elif !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Running() reaps already-exited children, avoiding stale PID reuse.
    if(Running()) { kill((pid_t)process, SIGKILL); while(waitpid((pid_t)process, nullptr, 0) < 0 && errno == EINTR) {} }
#endif
    process = job = 0;
}

#ifdef _WIN32
namespace {
wstring wide(const string &text)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    wstring result(n, L'\0');
    if(n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), &result[0], n);
    return result;
}
string utf8(const wchar_t *text)
{
    int length = (int)wcslen(text);
    int n = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    string result(n, '\0');
    if(n) WideCharToMultiByte(CP_UTF8, 0, text, length, &result[0], n, nullptr, nullptr);
    return result;
}
// Match the Windows CRT argv rules, including embedded quotes and trailing
// backslashes. No shell is involved, so map names cannot inject commands.
wstring quote(const wstring &arg)
{
    wstring result = L"\"";
    size_t slashes = 0;
    for(wchar_t c : arg) {
        if(c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'\"' ? 2 : 1), L'\\');
        if(c == L'\"') result += L'\\';
        result += c; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
bool listening(int port)
{
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(fd == INVALID_SOCKET) return false;
    u_long nonblocking = 1;
    bool ready = false;
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((unsigned short)port);
    if(ioctlsocket(fd, FIONBIO, &nonblocking) == 0) {
        ready = connect(fd, (sockaddr*)&address, sizeof(address)) == 0;
        if(!ready && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writes; FD_ZERO(&writes); FD_SET(fd, &writes);
            timeval timeout{0, 50000};
            if(select(0, nullptr, &writes, nullptr, &timeout) > 0) {
                int error = 0, length = sizeof(error);
                ready = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &length) == 0 && error == 0;
            }
        }
    }
    closesocket(fd);
    return ready;
}
}
#endif

bool ZLocalServer_Start(vector<string> args, int port, ZLocalServerProcess &owner, string &error_message, vector<string> settings)
{
	owner.Stop();
	error_message.clear();
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
	error_message = "Local hosting is unavailable on this platform";
	return false;
#elif defined(_WIN32)
    if(args.empty() || args[0].empty() || port < 1 || port > 65535) return false;
    WSADATA winsock;
    if(WSAStartup(MAKEWORD(2, 2), &winsock) != 0) { error_message = "Cannot initialize networking"; return false; }
    struct Network { ~Network() { WSACleanup(); } } network;
    wchar_t temp[MAX_PATH], logpath[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, temp);
    if(!length || length >= MAX_PATH || !GetTempFileNameW(temp, L"zod", 0, logpath)) {
        error_message = "Cannot create server log"; return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW(logpath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, 0, nullptr);
    if(log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if(log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if(input != INVALID_HANDLE_VALUE) CloseHandle(input);
        error_message = "Cannot open server log or input"; DeleteFileW(logpath); return false;
    }
    vector<wstring> environment;
    LPWCH inherited = GetEnvironmentStringsW();
    if(inherited) {
        for(const wchar_t *e = inherited; *e; e += wcslen(e) + 1)
            {
                bool overridden = !_wcsnicmp(e, L"ZOD_PORT=", 9) || !_wcsnicmp(e, L"ZOD_EMPTY_EXIT_SECS=", 20);
                for(const auto &setting : settings) {
                    wstring key = wide(setting.substr(0, setting.find('=') + 1));
                    if(!key.empty() && !_wcsnicmp(e, key.c_str(), key.size())) overridden = true;
                }
                if(!overridden) environment.emplace_back(e);
            }
        FreeEnvironmentStringsW(inherited);
    }
    for(const auto &setting : settings) environment.push_back(wide(setting));
    environment.push_back(L"ZOD_PORT=" + to_wstring(port));
    environment.push_back(L"ZOD_EMPTY_EXIT_SECS=900");
    sort(environment.begin(), environment.end(), [](const wstring &a, const wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    vector<wchar_t> env;
    for(const auto &value : environment) { env.insert(env.end(), value.begin(), value.end()); env.push_back(0); }
    env.push_back(0);
    wstring command;
    for(const auto &arg : args) { if(!command.empty()) command += L' '; command += quote(wide(arg)); }
    vector<wchar_t> command_line(command.begin(), command.end()); command_line.push_back(0);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log;
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    vector<unsigned char> attributes(attribute_size);
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();
    bool initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size) != 0;
    HANDLE handles[] = {log, input};
    bool configured = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    configured = configured && job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    PROCESS_INFORMATION child{};
    bool started = configured && CreateProcessW(wide(args[0]).c_str(), command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        env.data(), nullptr, &startup.StartupInfo, &child);
    DWORD error = GetLastError();
    if(initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(log); CloseHandle(input);
    if(!started) {
        if(job) CloseHandle(job);
        error_message = "Cannot start server (Windows error " + to_string(error) + ")";
        DeleteFileW(logpath); return false;
    }
    bool assigned = AssignProcessToJobObject(job, child.hProcess) != 0;
    bool resumed = assigned && ResumeThread(child.hThread) != (DWORD)-1;
    CloseHandle(child.hThread);
    if(!resumed) {
        TerminateProcess(child.hProcess, 1); WaitForSingleObject(child.hProcess, 5000);
        CloseHandle(child.hProcess); CloseHandle(job);
        error_message = "Cannot manage local server process"; return false;
    }
    owner.process = (intptr_t)child.hProcess; owner.job = (intptr_t)job;
    bool ready = false;
    auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
    while(chrono::steady_clock::now() < deadline && owner.Running()) {
        if(listening(port)) { ready = owner.Running(); break; }
        Sleep(100);
    }
    if(!ready) {
        bool exited = !owner.Running(); owner.Stop();
        error_message = string(exited ? "Server exited during startup. Log: " : "Server startup timed out. Log: ") + utf8(logpath);
        return false;
    }
    return true;
#else
	if(args.empty() || args[0].empty() || port < 1 || port > 65535) return false;
	char portbuf[16];
	snprintf(portbuf, sizeof(portbuf), "%d", port);
	// posix_spawn is safe from the menu's worker while SDL has other threads.
	// No allocator, setenv or other unsafe work runs in a forked child.
	char logpath[] = "/tmp/zod-match-XXXXXX";
	int logfd = mkstemp(logpath);
	if(logfd < 0) { error_message = "Cannot create server log"; return false; }
	vector<string> environment;
	for(char **e = environ; *e; ++e)
        {
            bool overridden = !strncmp(*e, "ZOD_PORT=", 9) || !strncmp(*e, "ZOD_EMPTY_EXIT_SECS=", 20);
            for(const auto &setting : settings) {
                string key = setting.substr(0, setting.find('=') + 1);
                if(!key.empty() && !strncmp(*e, key.c_str(), key.size())) overridden = true;
            }
            if(!overridden) environment.push_back(*e);
        }
    environment.insert(environment.end(), settings.begin(), settings.end());
	environment.push_back(string("ZOD_PORT=") + portbuf);
	environment.push_back("ZOD_EMPTY_EXIT_SECS=900");
	vector<char*> av, env;
	for(auto &arg : args) av.push_back(&arg[0]);
	av.push_back(nullptr);
	for(auto &value : environment) env.push_back(&value[0]);
	env.push_back(nullptr);
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, logfd, STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, logfd, STDERR_FILENO);
	posix_spawn_file_actions_addclose(&actions, logfd);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	pid_t pid;
	int error = posix_spawn(&pid, args[0].c_str(), &actions, nullptr, av.data(), env.data());
	posix_spawn_file_actions_destroy(&actions);
	close(logfd);
	if(error) { unlink(logpath); error_message = string("Cannot start server: ") + strerror(error); return false; }

	bool ready = false, exited = false;
	auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
	while(chrono::steady_clock::now() < deadline)
	{
		int status;
		pid_t waited = waitpid(pid, &status, WNOHANG);
		if(waited == pid || (waited < 0 && errno == ECHILD)) { exited = true; break; }
		// Check an actual listener, rather than interpreting a failed bind as readiness.
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if(fd >= 0 && fd < FD_SETSIZE)
		{
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons((unsigned short)port);
			int flags = fcntl(fd, F_GETFL, 0);
			if(flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0)
				ready = connect(fd, (sockaddr*)&address, sizeof(address)) == 0;
			if(!ready && errno == EINPROGRESS)
			{
				fd_set writes; FD_ZERO(&writes); FD_SET(fd, &writes);
				timeval timeout{0, 50000};
				if(select(fd + 1, nullptr, &writes, nullptr, &timeout) > 0)
				{
					int socket_error = 0; socklen_t size = sizeof(socket_error);
					ready = getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) == 0 && socket_error == 0;
				}
			}
		}
		if(fd >= 0) close(fd);
		if(ready) break;
		usleep(100000);
	}
	if(!ready)
	{
		if(!exited) { kill(pid, SIGKILL); while(waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }
		error_message = string(exited ? "Server exited during startup. Log: " : "Server startup timed out. Log: ") + logpath;
		return false;
	}

	int status;
	if(waitpid(pid, &status, WNOHANG) != 0)
	{
		error_message = string("Server exited during startup. Log: ") + logpath;
		return false;
	}
	owner.process = pid;
	return true;
#endif
}

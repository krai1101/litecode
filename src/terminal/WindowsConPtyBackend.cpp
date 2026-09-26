#include "terminal/WindowsConPtyBackend.h"

#include "terminal/TerminalInputQueue.h"

#include <QDir>
#include <QHash>
#include <QMultiHash>
#include <QSet>
#include <QThread>
#include <QTimer>

// Windows requires the base declarations before the tool-help declarations.
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace litecode::terminal {
namespace {

constexpr int pollIntervalMs = 10;
constexpr DWORD processPollMs = 25;
constexpr DWORD foregroundProcessPollMs = 500;
constexpr DWORD gracefulStopMs = 500;
constexpr qsizetype ioChunkSize = 16 * 1024;

class UniqueHandle final {
  public:
    explicit UniqueHandle(std::shared_ptr<WindowsConPtyResourceAudit> audit = {})
        : audit_(std::move(audit)) {}
    UniqueHandle(HANDLE handle, std::shared_ptr<WindowsConPtyResourceAudit> audit)
        : audit_(std::move(audit)) {
        reset(handle);
    }
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)), audit_(std::move(other.audit_)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
            audit_ = std::move(other.audit_);
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const { return handle_; }
    void reset(HANDLE handle = nullptr) {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            if (audit_)
                --audit_->handles;
        }
        handle_ = handle;
        if (handle_ && handle_ != INVALID_HANDLE_VALUE && audit_)
            ++audit_->handles;
    }

  private:
    HANDLE handle_{};
    std::shared_ptr<WindowsConPtyResourceAudit> audit_;
};

class UniquePseudoConsole final {
  public:
    explicit UniquePseudoConsole(std::shared_ptr<WindowsConPtyResourceAudit> audit = {})
        : audit_(std::move(audit)) {}
    ~UniquePseudoConsole() { reset(); }
    UniquePseudoConsole(const UniquePseudoConsole&) = delete;
    UniquePseudoConsole& operator=(const UniquePseudoConsole&) = delete;
    [[nodiscard]] HPCON get() const { return value_; }
    [[nodiscard]] HPCON* address() { return &value_; }
    void reset() {
        if (value_) {
            ClosePseudoConsole(std::exchange(value_, nullptr));
            if (audit_)
                --audit_->pseudoconsoles;
        }
    }
    void markCreated() {
        if (value_ && audit_)
            ++audit_->pseudoconsoles;
    }

  private:
    HPCON value_{};
    std::shared_ptr<WindowsConPtyResourceAudit> audit_;
};

class UniqueAttributeList final {
  public:
    explicit UniqueAttributeList(std::shared_ptr<WindowsConPtyResourceAudit> audit = {})
        : audit_(std::move(audit)) {}
    ~UniqueAttributeList() { reset(); }
    UniqueAttributeList(const UniqueAttributeList&) = delete;
    UniqueAttributeList& operator=(const UniqueAttributeList&) = delete;
    bool allocate(SIZE_T bytes) {
        value_ =
            reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, bytes));
        if (value_ && audit_)
            ++audit_->attributeLists;
        return value_ != nullptr;
    }
    [[nodiscard]] PPROC_THREAD_ATTRIBUTE_LIST get() const { return value_; }
    void markInitialized() { initialized_ = true; }
    void reset() {
        if (!value_)
            return;
        if (initialized_)
            DeleteProcThreadAttributeList(value_);
        HeapFree(GetProcessHeap(), 0, value_);
        if (audit_)
            --audit_->attributeLists;
        value_ = nullptr;
        initialized_ = false;
    }

  private:
    PPROC_THREAD_ATTRIBUTE_LIST value_{};
    bool initialized_{false};
    std::shared_ptr<WindowsConPtyResourceAudit> audit_;
};

QString windowsError(const QString& prefix, unsigned long code) {
    return QStringLiteral("%1 (Windows error 0x%2)").arg(prefix, QString::number(code, 16));
}

std::wstring quoteWindowsArgument(const QString& value) {
    const std::wstring argument = value.toStdWString();
    if (argument.find_first_of(L" \t\"") == std::wstring::npos)
        return argument;
    std::wstring quoted(1, L'"');
    size_t slashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            slashes = 0;
            continue;
        }
        quoted.append(slashes, L'\\');
        slashes = 0;
        quoted.push_back(character);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::vector<wchar_t> commandLineFor(const TerminalProfile& profile) {
    std::wstring command = quoteWindowsArgument(profile.executable);
    for (const QString& argument : profile.arguments) {
        command.push_back(L' ');
        command.append(quoteWindowsArgument(argument));
    }
    std::vector<wchar_t> result(command.begin(), command.end());
    result.push_back(L'\0');
    return result;
}

std::vector<wchar_t> environmentBlock(const QProcessEnvironment& environment) {
    const QStringList entries = environment.toStringList();
    std::vector<wchar_t> block;
    for (const QString& entry : entries) {
        const std::wstring encoded = entry.toStdWString();
        block.insert(block.end(), encoded.begin(), encoded.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

QString compactExecutableName(const wchar_t* executable) {
    QString name = QString::fromWCharArray(executable).trimmed();
    const qsizetype slash =
        std::max(name.lastIndexOf(QLatin1Char('\\')), name.lastIndexOf(QLatin1Char('/')));
    if (slash >= 0)
        name = name.mid(slash + 1);
    if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        name.chop(4);
    return name.toLower();
}

bool isShellExecutable(const QString& name) {
    static const QStringList shells{
        QStringLiteral("cmd"),    QStringLiteral("powershell"),  QStringLiteral("pwsh"),
        QStringLiteral("bash"),   QStringLiteral("git-cmd"),     QStringLiteral("wsl"),
        QStringLiteral("ubuntu"), QStringLiteral("ubuntu1804"),  QStringLiteral("kali"),
        QStringLiteral("debian"), QStringLiteral("opensuse-42"), QStringLiteral("sles-12"),
        QStringLiteral("julia"),  QStringLiteral("nu"),          QStringLiteral("node"),
        QStringLiteral("xonsh")};
    if (shells.contains(name))
        return true;
    if (!name.startsWith(QStringLiteral("python")))
        return false;
    const QString suffix = name.mid(6);
    if (suffix.isEmpty())
        return true;
    bool ok = false;
    suffix.toDouble(&ok);
    return ok;
}

QString foregroundProcessName(DWORD rootProcessId) {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0), {});
    if (snapshot.get() == INVALID_HANDLE_VALUE)
        return {};

    struct ProcessInfo final {
        DWORD parent{};
        QString name;
        QVector<DWORD> children;
    };
    QHash<DWORD, ProcessInfo> processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.get(), &entry)) {
        do {
            processes.insert(
                entry.th32ProcessID,
                {entry.th32ParentProcessID, compactExecutableName(entry.szExeFile), {}});
        } while (Process32NextW(snapshot.get(), &entry));
    }

    if (!processes.contains(rootProcessId))
        return {};
    for (auto it = processes.cbegin(); it != processes.cend(); ++it) {
        auto parent = processes.find(it->parent);
        if (parent != processes.end())
            parent->children.push_back(it.key());
    }

    DWORD current = rootProcessId;
    for (int depth = 0; depth <= processes.size(); ++depth) {
        const auto node = processes.constFind(current);
        if (node == processes.cend())
            return {};
        if (!isShellExecutable(node->name) || node->children.isEmpty())
            return node->name;

        DWORD next = 0;
        for (const DWORD childId : node->children) {
            const auto child = processes.constFind(childId);
            if (child != processes.cend() && child->name != QStringLiteral("conhost")) {
                next = childId;
                break;
            }
        }
        if (next == 0)
            return node->name;
        current = next;
    }
    return processes.value(current).name;
}

QVector<DWORD> descendantProcessIds(DWORD rootProcessId) {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0), {});
    if (snapshot.get() == INVALID_HANDLE_VALUE)
        return {};

    QMultiHash<DWORD, DWORD> children;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.get(), &entry)) {
        do {
            if (entry.th32ProcessID != 0 && entry.th32ProcessID != rootProcessId)
                children.insert(entry.th32ParentProcessID, entry.th32ProcessID);
        } while (Process32NextW(snapshot.get(), &entry));
    }

    QVector<DWORD> descendants;
    QVector<DWORD> pending{rootProcessId};
    QSet<DWORD> visited{rootProcessId};
    while (!pending.isEmpty()) {
        const DWORD parent = pending.takeLast();
        const QList<DWORD> directChildren = children.values(parent);
        for (const DWORD child : directChildren) {
            if (visited.contains(child))
                continue;
            visited.insert(child);
            descendants.push_back(child);
            pending.push_back(child);
        }
    }
    return descendants;
}

void terminateProcessTreeFallback(DWORD rootProcessId, HANDLE rootProcess, bool terminateRoot) {
    QVector<DWORD> descendants = descendantProcessIds(rootProcessId);
    std::reverse(descendants.begin(), descendants.end());
    for (const DWORD processId : descendants) {
        UniqueHandle process(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId), {});
        if (process.get())
            (void)TerminateProcess(process.get(), 1);
    }
    if (terminateRoot && rootProcess)
        (void)TerminateProcess(rootProcess, 1);
}

} // namespace

struct WindowsConPtyBackend::RunState final {
    std::mutex mutex;
    std::condition_variable inputReady;
    std::condition_variable outputSpaceAvailable;
    core::OperationId operationId;
    TerminalState workerState{TerminalState::Starting};
    WindowsConPtyFailurePoint failurePoint{WindowsConPtyFailurePoint::None};
    QSize requestedSize{120, 32};
    QSize appliedSize{120, 32};
    bool sizeChanged{false};
    bool stopRequested{false};
    bool readerStopRequested{false};
    bool startedPending{false};
    bool supervisorFinished{false};
    bool completionDelivered{false};
    QString foregroundProcessName;
    bool foregroundProcessChangedPending{false};
    TerminalInputQueue input;
    QByteArray output;
    std::optional<TerminalExit> completion;
    std::shared_ptr<WindowsConPtyResourceAudit> resourceAudit;
};

namespace {

TerminalExit failureExit(core::OperationId operationId, const QString& diagnostic) {
    TerminalExit result;
    result.operationId = operationId;
    result.completion = core::CompletionKind::Failed;
    result.safeDiagnostic = diagnostic;
    return result;
}

bool queueOutput(const std::shared_ptr<WindowsConPtyBackend::RunState>& run, const char* bytes,
                 qsizetype size) {
    std::unique_lock lock(run->mutex);
    run->outputSpaceAvailable.wait(lock, [&] {
        return run->readerStopRequested || run->output.size() + size <= terminalOutputQueueLimit;
    });
    if (run->readerStopRequested)
        return false;
    run->output.append(bytes, size);
    return true;
}

TerminalExit executeConPty(const std::shared_ptr<WindowsConPtyBackend::RunState>& run,
                           const TerminalProfile& profile, const QString& workingDirectory) {
    // Destruction is reversed: close the output/input client endpoints before ClosePseudoConsole.
    // That is the shutdown order required by the ConPTY contract on supported Windows releases.
    UniquePseudoConsole console(run->resourceAudit);
    UniqueHandle ptyInputRead(run->resourceAudit);
    UniqueHandle inputWrite(run->resourceAudit);
    UniqueHandle outputRead(run->resourceAudit);
    UniqueHandle ptyOutputWrite(run->resourceAudit);
    HANDLE first = nullptr;
    HANDLE second = nullptr;
    if (run->failurePoint == WindowsConPtyFailurePoint::InputPipe ||
        !CreatePipe(&first, &second, nullptr, 0)) {
        return failureExit(
            run->operationId,
            windowsError(QStringLiteral("Unable to create terminal input pipe"), GetLastError()));
    }
    ptyInputRead.reset(first);
    inputWrite.reset(second);
    first = nullptr;
    second = nullptr;
    if (run->failurePoint == WindowsConPtyFailurePoint::OutputPipe ||
        !CreatePipe(&first, &second, nullptr, 0)) {
        return failureExit(
            run->operationId,
            windowsError(QStringLiteral("Unable to create terminal output pipe"), GetLastError()));
    }
    outputRead.reset(first);
    ptyOutputWrite.reset(second);

    QSize requestedInitialSize;
    {
        std::lock_guard lock(run->mutex);
        requestedInitialSize = run->requestedSize;
    }
    const COORD initialSize{
        static_cast<SHORT>(std::clamp(requestedInitialSize.width(), 1, 32767)),
        static_cast<SHORT>(std::clamp(requestedInitialSize.height(), 1, 32767))};
    const HRESULT pseudoResult =
        run->failurePoint == WindowsConPtyFailurePoint::PseudoConsole
            ? E_FAIL
            : CreatePseudoConsole(initialSize, ptyInputRead.get(), ptyOutputWrite.get(), 0,
                                  console.address());
    if (FAILED(pseudoResult)) {
        return failureExit(run->operationId,
                           windowsError(QStringLiteral("Unable to create ConPTY"), pseudoResult));
    }
    console.markCreated();

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    UniqueAttributeList attributes(run->resourceAudit);
    if (run->failurePoint == WindowsConPtyFailurePoint::AttributeAllocation ||
        !attributes.allocate(attributeBytes)) {
        return failureExit(run->operationId,
                           QStringLiteral("Unable to allocate ConPTY startup information."));
    }
    if (run->failurePoint == WindowsConPtyFailurePoint::AttributeInitialization ||
        !InitializeProcThreadAttributeList(attributes.get(), 1, 0, &attributeBytes)) {
        return failureExit(
            run->operationId,
            windowsError(QStringLiteral("Unable to initialize ConPTY startup"), GetLastError()));
    }
    attributes.markInitialized();
    HPCON consoleValue = console.get();
    if (run->failurePoint == WindowsConPtyFailurePoint::AttributeUpdate ||
        !UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   consoleValue, sizeof(consoleValue), nullptr, nullptr)) {
        return failureExit(
            run->operationId,
            windowsError(QStringLiteral("Unable to configure ConPTY"), GetLastError()));
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    // Explicit null standard handles prevent CreateProcess from duplicating redirected parent
    // handles into a console child. The child then receives its standard streams from ConPTY.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION processInfo{};
    std::vector<wchar_t> command = commandLineFor(profile);
    std::vector<wchar_t> environment = environmentBlock(profile.environment);
    const std::wstring cwd = QDir::toNativeSeparators(workingDirectory).toStdWString();
    const BOOL processCreated =
        run->failurePoint == WindowsConPtyFailurePoint::ProcessCreation
            ? FALSE
            : CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                             environment.data(), cwd.empty() ? nullptr : cwd.c_str(),
                             &startup.StartupInfo, &processInfo);
    if (!processCreated) {
        return failureExit(
            run->operationId,
            windowsError(QStringLiteral("Unable to start terminal profile"), GetLastError()));
    }
    // ConPTY retains the server-side pipe endpoints only after its client process attaches.
    // Closing them between CreatePseudoConsole and CreateProcess races startup and can make an
    // otherwise valid interactive shell observe EOF and exit immediately.
    ptyInputRead.reset();
    ptyOutputWrite.reset();
    UniqueHandle processJob(run->resourceAudit);
    UniqueHandle process(processInfo.hProcess, run->resourceAudit);
    UniqueHandle processThread(processInfo.hThread, run->resourceAudit);
    // Keep the shell and everything it launches in a kill-on-close job. Terminating only the
    // shell process leaves commands such as compilers or language servers orphaned.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        processJob.reset(job);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (run->failurePoint == WindowsConPtyFailurePoint::JobAssignment ||
            !SetInformationJobObject(processJob.get(), JobObjectExtendedLimitInformation, &limits,
                                     sizeof(limits)) ||
            !AssignProcessToJobObject(processJob.get(), process.get())) {
            processJob.reset();
        }
    }
    attributes.reset();

    std::thread reader;
    std::thread writer;
    try {
        if (run->failurePoint == WindowsConPtyFailurePoint::WriterCreation)
            throw std::system_error(
                std::make_error_code(std::errc::resource_unavailable_try_again));
        writer = std::thread([run, handle = inputWrite.get()] {
            for (;;) {
                QByteArray bytes;
                {
                    std::unique_lock lock(run->mutex);
                    run->inputReady.wait(
                        lock, [&] { return run->stopRequested || !run->input.isEmpty(); });
                    if (run->stopRequested)
                        return;
                    bytes = run->input.nextSlice(ioChunkSize);
                }
                DWORD written = 0;
                if (!WriteFile(handle, bytes.constData(), static_cast<DWORD>(bytes.size()),
                               &written, nullptr) ||
                    written == 0)
                    return;
                std::lock_guard lock(run->mutex);
                if (run->stopRequested)
                    return;
                run->input.consume(static_cast<qsizetype>(written));
            }
        });
        if (run->failurePoint == WindowsConPtyFailurePoint::ReaderCreation)
            throw std::system_error(
                std::make_error_code(std::errc::resource_unavailable_try_again));
        reader = std::thread([run, handle = outputRead.get()] {
            QByteArray buffer(ioChunkSize, Qt::Uninitialized);
            for (;;) {
                DWORD count = 0;
                if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count,
                              nullptr) ||
                    count == 0)
                    return;
                if (!queueOutput(run, buffer.constData(), static_cast<qsizetype>(count)))
                    return;
            }
        });
    } catch (const std::system_error&) {
        {
            std::lock_guard lock(run->mutex);
            run->stopRequested = true;
            run->readerStopRequested = true;
        }
        run->inputReady.notify_all();
        run->outputSpaceAvailable.notify_all();
        if (processJob.get())
            (void)TerminateProcess(process.get(), 1);
        else
            terminateProcessTreeFallback(processInfo.dwProcessId, process.get(), true);
        if (reader.joinable()) {
            CancelSynchronousIo(reader.native_handle());
            reader.join();
        }
        if (writer.joinable())
            writer.join();
        return failureExit(run->operationId,
                           QStringLiteral("Unable to create terminal I/O workers."));
    }

    {
        std::lock_guard lock(run->mutex);
        run->workerState = TerminalState::Running;
        run->startedPending = true;
    }

    bool cancellationRequested = false;
    DWORD foregroundPollElapsed = foregroundProcessPollMs;
    for (;;) {
        const DWORD waitResult = WaitForSingleObject(process.get(), processPollMs);
        if (waitResult == WAIT_OBJECT_0)
            break;
        foregroundPollElapsed += processPollMs;
        if (foregroundPollElapsed >= foregroundProcessPollMs) {
            foregroundPollElapsed = 0;
            const QString name = foregroundProcessName(processInfo.dwProcessId);
            if (!name.isEmpty()) {
                std::lock_guard lock(run->mutex);
                if (name != run->foregroundProcessName) {
                    run->foregroundProcessName = name;
                    run->foregroundProcessChangedPending = true;
                }
            }
        }
        QSize requested;
        bool resize = false;
        {
            std::lock_guard lock(run->mutex);
            cancellationRequested = run->stopRequested;
            resize = run->sizeChanged;
            requested = run->requestedSize;
            run->sizeChanged = false;
        }
        if (resize && requested != run->appliedSize) {
            ResizePseudoConsole(console.get(),
                                {static_cast<SHORT>(std::clamp(requested.width(), 1, 32767)),
                                 static_cast<SHORT>(std::clamp(requested.height(), 1, 32767))});
            run->appliedSize = requested;
        }
        if (!cancellationRequested)
            continue;
        inputWrite.reset();
        if (WaitForSingleObject(process.get(), gracefulStopMs) == WAIT_TIMEOUT) {
            if (processJob.get())
                (void)TerminateProcess(process.get(), 1);
            else
                terminateProcessTreeFallback(processInfo.dwProcessId, process.get(), true);
            WaitForSingleObject(process.get(), INFINITE);
        }
        break;
    }

    if (!processJob.get())
        terminateProcessTreeFallback(processInfo.dwProcessId, process.get(), false);

    DWORD exitCode = static_cast<DWORD>(-1);
    GetExitCodeProcess(process.get(), &exitCode);
    {
        std::lock_guard lock(run->mutex);
        run->stopRequested = true;
    }
    run->inputReady.notify_all();
    inputWrite.reset();
    console.reset();
    if (reader.joinable()) {
        CancelSynchronousIo(reader.native_handle());
        reader.join();
    }
    if (writer.joinable()) {
        CancelSynchronousIo(writer.native_handle());
        writer.join();
    }
    outputRead.reset();

    TerminalExit result;
    result.operationId = run->operationId;
    result.completion =
        cancellationRequested ? core::CompletionKind::Cancelled : core::CompletionKind::Succeeded;
    result.exitCode = static_cast<int>(exitCode);
    // A non-zero shell exit is not a crash. Windows exception status codes occupy the
    // 0xC0000000 range; ordinary `exit /b N` values must remain truthful normal exits.
    result.crashed = !cancellationRequested && (exitCode & 0xC0000000U) == 0xC0000000U;
    return result;
}

void superviseConPty(const std::shared_ptr<WindowsConPtyBackend::RunState>& run,
                     TerminalProfile profile, QString workingDirectory) {
    TerminalExit result;
    try {
        result = executeConPty(run, profile, workingDirectory);
    } catch (const std::exception&) {
        result = failureExit(run->operationId,
                             QStringLiteral("The terminal supervisor failed unexpectedly."));
    } catch (...) {
        result = failureExit(run->operationId,
                             QStringLiteral("The terminal supervisor failed unexpectedly."));
    }

    // `executeConPty` has returned, so every stack-owned handle, pseudoconsole, attribute list,
    // and I/O worker has been reclaimed. Completion is deliberately published last.
    std::lock_guard lock(run->mutex);
    run->workerState = TerminalState::Stopped;
    run->completion = std::move(result);
}

} // namespace

WindowsConPtyBackend::WindowsConPtyBackend(
    QObject* parent, WindowsConPtyFailurePoint failurePoint,
    std::shared_ptr<WindowsConPtyResourceAudit> resourceAudit)
    : ITerminalBackend(parent), pollTimer_(new QTimer(this)), failurePoint_(failurePoint),
      resourceAudit_(resourceAudit ? std::move(resourceAudit)
                                   : std::make_shared<WindowsConPtyResourceAudit>()) {
    pollTimer_->setInterval(pollIntervalMs);
    connect(pollTimer_, &QTimer::timeout, this, &WindowsConPtyBackend::pollRun);
}

WindowsConPtyBackend::~WindowsConPtyBackend() {
    requestStop();
    run_.reset();
}

void WindowsConPtyBackend::start(core::OperationId operationId, const TerminalProfile& profile,
                                 const QString& workingDirectory) {
    if (state_ != TerminalState::Stopped || !profile.isValid()) {
        emit errorOccurred(tr("The terminal profile is invalid or a terminal is already active."));
        return;
    }
    auto run = std::make_shared<RunState>();
    run->operationId = operationId;
    run->failurePoint = failurePoint_;
    run->resourceAudit = resourceAudit_;
    run->requestedSize = terminalSize_;
    run->appliedSize = terminalSize_;
    run_ = run;
    setState(TerminalState::Starting);
    pollTimer_->start();
    QThread* supervisor = QThread::create(
        [run, profile, workingDirectory] { superviseConPty(run, profile, workingDirectory); });
    connect(supervisor, &QThread::finished, supervisor, &QObject::deleteLater);
    connect(supervisor, &QThread::finished, this, [this, run] {
        {
            std::lock_guard lock(run->mutex);
            run->supervisorFinished = true;
        }
        pollRun();
    });
    supervisor->start();
}

bool WindowsConPtyBackend::enqueueInput(const QByteArray& bytes) {
    if (bytes.isEmpty())
        return true;
    const auto run = run_;
    if (!run || state_ != TerminalState::Running) {
        emit inputRejected(run ? run->operationId : core::OperationId{},
                           tr("The terminal is not accepting input."));
        return false;
    }
    bool rejected = false;
    {
        std::lock_guard lock(run->mutex);
        rejected = !run->input.enqueue(bytes);
    }
    if (rejected) {
        emit inputRejected(run->operationId, tr("Terminal input queue is full."));
        return false;
    }
    run->inputReady.notify_one();
    return true;
}

void WindowsConPtyBackend::resizeTerminal(const QSize& characters) {
    const QSize requested(std::clamp(characters.width(), 1, 32767),
                          std::clamp(characters.height(), 1, 32767));
    terminalSize_ = requested;
    const auto run = run_;
    if (!run)
        return;
    std::lock_guard lock(run->mutex);
    run->requestedSize = requested;
    run->sizeChanged = true;
}

void WindowsConPtyBackend::requestStop() {
    const auto run = run_;
    if (!run || state_ == TerminalState::Stopping || state_ == TerminalState::Stopped)
        return;
    {
        std::lock_guard lock(run->mutex);
        run->stopRequested = true;
        run->readerStopRequested = true;
        run->workerState = TerminalState::Stopping;
    }
    run->inputReady.notify_all();
    run->outputSpaceAvailable.notify_all();
    setState(TerminalState::Stopping);
}

TerminalState WindowsConPtyBackend::state() const { return state_; }

void WindowsConPtyBackend::pollRun() {
    const auto run = run_;
    if (!run) {
        pollTimer_->stop();
        return;
    }
    QByteArray output;
    bool started = false;
    QString foregroundProcess;
    TerminalState workerState;
    std::optional<TerminalExit> completion;
    {
        std::lock_guard lock(run->mutex);
        workerState = run->workerState;
        started = std::exchange(run->startedPending, false);
        if (std::exchange(run->foregroundProcessChangedPending, false))
            foregroundProcess = run->foregroundProcessName;
        const qsizetype count = std::min(run->output.size(), terminalOutputDrainSlice);
        output = run->output.first(count);
        run->output.remove(0, count);
        if (run->supervisorFinished && run->completion && run->output.isEmpty() &&
            !run->completionDelivered) {
            completion = run->completion;
            run->completionDelivered = true;
        }
    }
    run->outputSpaceAvailable.notify_one();
    if (workerState != state_)
        setState(workerState);
    if (started)
        emit this->started(run->operationId);
    if (!foregroundProcess.isEmpty())
        emit foregroundProcessChanged(run->operationId, foregroundProcess);
    if (!output.isEmpty())
        emit outputReady(output);
    if (!completion)
        return;
    if (state_ != TerminalState::Stopped)
        setState(TerminalState::Stopped);
    if (!completion->safeDiagnostic.isEmpty())
        emit errorOccurred(completion->safeDiagnostic);
    emit completed(*completion);
    pollTimer_->stop();
    run_.reset();
}

void WindowsConPtyBackend::setState(TerminalState state) {
    if (state_ == state)
        return;
    state_ = state;
    const auto run = run_;
    emit stateChanged(run ? run->operationId : core::OperationId{}, state_);
}

} // namespace litecode::terminal

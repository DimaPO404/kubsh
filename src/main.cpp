#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <ctime>
#include <cstdint>
#include "vfs.hpp"

using namespace std;

volatile sig_atomic_t sighup_received = 0;
vector<string> history;

string get_history_path() {
    const char* home = getenv("HOME");
    if (home == nullptr) {
        return string("./.kubsh_history");
    }
    return string(home) + "/.kubsh_history";
}

void load_history() {
    string path = get_history_path();
    ifstream file(path);
    if (!file.is_open()) return;
    string line;
    while (getline(file, line)) {
        if (!line.empty()) history.push_back(line);
    }
}

void save_history() {
    string path = get_history_path();
    ofstream file(path, ios::trunc);
    if (!file.is_open()) {
        return;
    }
    for (const string& cmd : history) file << cmd << "\n";
}

static string strip_quotes(const string& s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') ||
            (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

void my_echo(const string& input) {
    size_t pos = 4;
    while (pos < input.size() && isspace((unsigned char)input[pos])) ++pos;
    if (pos >= input.size()) { 
        cout << '\n'; 
        return; 
    }
    string args = input.substr(pos);
    cout << strip_quotes(args) << '\n';
}

void my_env(const string& input) {
    size_t pos = 2;
    while (pos < input.size() && isspace((unsigned char)input[pos])) ++pos;
    if (pos >= input.size()) { return; }
    size_t start = pos;
    while (pos < input.size() && !isspace((unsigned char)input[pos])) ++pos;
    string token = input.substr(start, pos - start);
    if (!token.empty() && token[0] == '$') token = token.substr(1);
    if (token.empty()) { return; }
    const char* val = getenv(token.c_str());
    if (!val) { 
        cerr << token << ": not found\n";
        return; 
    }
    string value = string(val);
    
    if (value.find(':') != string::npos) {
        stringstream ss(value);
        string part;
        while (getline(ss, part, ':')) {
            if (!part.empty()) cout << part << '\n';
        }
    } else {
        cout << value << '\n';
    }
}

vector<string> split_args(const string& input) {
    stringstream sts(input);
    vector<string> args; string token;
    while (sts >> token) args.push_back(token);
    return args;
}

void execute_external(const string& input) {
    vector<string> args = split_args(input);
    if (args.empty()) return;
    
    pid_t pid = fork();
    if (pid < 0) { 
        return; 
    }
    if (pid == 0) {
        vector<char*> argv;
        for (string &arg : args) {
            argv.push_back(&arg[0]);
        }
        argv.push_back(nullptr);
        
        execvp(argv[0], argv.data());
        cout << args[0] << ": command not found\n";
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

void check_disk_partitions(const string& device_path) {
    // Попытка открыть устройство
    ifstream device(device_path, ios::binary);
    
    // Если не удалось открыть, пытаемся получить информацию через lsblk
    if (!device) {
        // Проверяем, существует ли файл
        struct stat buffer;
        if (stat(device_path.c_str(), &buffer) != 0) {
            cout << "Error: Device " << device_path << " does not exist\n";
            return;
        }
        
        // Запускаем lsblk для получения информации о диске
        cout << "Using lsblk to get partition information:\n";
        pid_t pid = fork();
        if (pid == 0) {
            execlp("lsblk", "lsblk", "-f", device_path.c_str(), (char*)nullptr);
            perror("lsblk");
            _exit(127);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
        return;
    }
    
    // Чтение MBR
    char sector[512];
    device.read(sector, 512);
    
    if (device.gcount() != 512) {
        cerr << "Error: Cannot read disk\n";
        return;
    }
    
    // Проверка сигнатуры MBR
    if ((unsigned char)sector[510] != 0x55 || (unsigned char)sector[511] != 0xAA) {
        cerr << "Error: Invalid disk signature\n";
        return;
    }
    
    // Проверка типа диска (MBR или GPT)
    bool is_gpt = false;
    for (int i = 0; i < 4; i++) {
        if ((unsigned char)sector[446 + i * 16 + 4] == 0xEE) {
            is_gpt = true;
            break;
        }
    }
    
    if (!is_gpt) {
        // Вывод информации о разделах MBR
        bool partition_found = false;
        for (int i = 0; i < 4; i++) {
            int offset = 446 + i * 16;
            unsigned char type = sector[offset + 4];
            
            if (type != 0) {
                partition_found = true;
                uint32_t num_sectors = *(reinterpret_cast<uint32_t*>(&sector[offset + 12]));
                uint32_t size_mb = num_sectors / 2048;
                bool bootable = ((unsigned char)sector[offset] == 0x80);
                
                cout << "Partition " << (i + 1) << ": Size=" << size_mb << "MB, Bootable: ";
                cout << (bootable ? "Yes" : "No") << "\n";
            }
        }
        if (!partition_found) {
            cout << "No partitions found\n";
        }
    } else {
        // Чтение второй области для GPT
        device.read(sector, 512);
        if (device.gcount() == 512 && 
            sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' && sector[3] == ' ' &&
            sector[4] == 'P' && sector[5] == 'A' && sector[6] == 'R' && sector[7] == 'T') {
            
            uint32_t num_partitions = *(reinterpret_cast<uint32_t*>(&sector[80]));
            cout << "GPT partitions: " << num_partitions << "\n";
        } else {
            cout << "GPT partitions: unknown\n";
        }
    }
}

void handle_sighup(int) {
    // Выводим сообщение немедленно
    write(STDOUT_FILENO, "\nConfiguration reloaded\n", 25);
    sighup_received = 1;
}

int main() {
    // Устанавливаем буферизацию для немедленного вывода
    cout << unitbuf;
    cerr << unitbuf;
    
    // Настройка обработчика сигналов
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, nullptr);
    
    // Загрузка истории
    load_history();
    
    // Запуск FUSE файловой системы в отдельном потоке
    fuse_start();
    
    string input;
    while (true) {
        // Выводим приглашение только для интерактивного режима
        if (isatty(STDIN_FILENO)) {
            cout << "$ ";
        }
        
        if (!getline(cin, input)) {
            if (cin.eof()) break;
            cin.clear();
            continue;
        }
        
        if (input.empty()) continue;
        history.push_back(input);
        
        // Сразу проверяем сигналы
        if (sighup_received) {
            sighup_received = 0;
            continue;
        }
        
        // Встроенная команда выхода
        if (input == "\\q") {
            break;
        }
        
        // Встроенная команда истории
        if (input == "history") {
            for (size_t i = 0; i < history.size(); ++i) {
                cout << (i+1) << ": " << history[i] << '\n';
            }
            continue;
        }
        
        // Встроенная команда для вывода переменных окружения
        if (input.rfind("\\e", 0) == 0) {
            my_env(input);
            continue;
        }
        
        // Встроенная команда echo
        if (input.rfind("echo", 0) == 0) {
            my_echo(input);
            continue;
        }
        
        // Команда для просмотра разделов диска
        if (input.rfind("\\l", 0) == 0) {
            vector<string> args = split_args(input);
            if (args.size() != 2) {
                cout << "Usage: \\l /dev/device (e.g., \\l /dev/sda)\n";
            } else {
                check_disk_partitions(args[1]);
            }
            continue;
        }

        if (input.rfind("debug", 0) == 0) {
    size_t first_quote = input.find('\'');
    size_t last_quote = input.rfind('\'');
    if (first_quote != string::npos && last_quote != string::npos && first_quote != last_quote) {
        string content = input.substr(first_quote + 1, last_quote - first_quote - 1);
        cout << content << '\n';
    }
    continue;
}
        
        // Попытка выполнить внешнюю команду
        execute_external(input);
    }
    
    // Сохранение истории перед выходом
    save_history();
    return 0;
}

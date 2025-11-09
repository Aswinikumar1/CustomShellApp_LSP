#include <iostream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <map>
#include <algorithm>  // Added for std::find

using namespace std;

// Track background jobs
map<pid_t, string> jobs;

// Handle Ctrl+C (SIGINT)
void handle_sigint(int sig) {
    cout << "\nUse 'exit' to quit the shell.\n";
}

// Handle Ctrl+Z (SIGTSTP)
void handle_sigtstp(int sig) {
    cout << "\nSuspending shell is disabled.\n";
}

// Split input string into tokens
vector<string> tokenize(string input) {
    vector<string> tokens;
    stringstream ss(input);
    string token;
    while (ss >> token) tokens.push_back(token);
    return tokens;
}

// Convert vector<string> → vector<char*> (for execvp)
vector<char*> vec_to_char_array(vector<string> &args) {
    vector<char*> cargs;
    for (auto &s : args) cargs.push_back(const_cast<char*>(s.c_str()));
    cargs.push_back(NULL);
    return cargs;
}

// Execute a basic command
void executeCommand(vector<string> args, bool background) {
    vector<char*> cargs = vec_to_char_array(args);
    pid_t pid = fork();

    if (pid == 0) {
        if (execvp(cargs[0], cargs.data()) == -1)
            perror("Execution failed");
        exit(1);
    } else if (pid > 0) {
        if (background) {
            cout << "[Running in background] PID: " << pid << endl;
            jobs[pid] = args[0];
        } else {
            waitpid(pid, NULL, 0);
        }
    } else perror("Fork failed");
}

// Handle I/O redirection (> or <)
void handleRedirection(vector<string> tokens, bool background) {
    int in = -1, out = -1;
    vector<string> args;
    
    for (int i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "<") {
            in = open(tokens[i+1].c_str(), O_RDONLY);
            i++;
        } else if (tokens[i] == ">") {
            out = open(tokens[i+1].c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            i++;
        } else {
            args.push_back(tokens[i]);
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (in != -1) dup2(in, STDIN_FILENO);
        if (out != -1) dup2(out, STDOUT_FILENO);
        if (in != -1) close(in);
        if (out != -1) close(out);

        vector<char*> cargs = vec_to_char_array(args);
        if (execvp(cargs[0], cargs.data()) == -1) perror("Exec failed");
        exit(1);
    } else if (pid > 0) {
        if (background) jobs[pid] = args[0];
        else waitpid(pid, NULL, 0);
    } else perror("Fork failed");
}

// Handle piping (|) between two commands
void handlePipes(vector<string> tokens, bool background) {
    int pipePos = -1;
    for (int i = 0; i < tokens.size(); i++)
        if (tokens[i] == "|") pipePos = i;

    if (pipePos == -1) return;

    vector<string> left(tokens.begin(), tokens.begin() + pipePos);
    vector<string> right(tokens.begin() + pipePos + 1, tokens.end());

    int pipefd[2];
    pipe(pipefd);
    pid_t pid1 = fork();

    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        vector<char*> cargs = vec_to_char_array(left);
        execvp(cargs[0], cargs.data());
        perror("Pipe exec failed");
        exit(1);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[1]);
        close(pipefd[0]);
        vector<char*> cargs = vec_to_char_array(right);
        execvp(cargs[0], cargs.data());
        perror("Pipe exec failed");
        exit(1);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    if (!background) {
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
    }
}

// Show list of running background jobs
void listJobs() {
    cout << "\nActive Background Jobs:\n";
    for (auto &job : jobs)
        cout << "PID: " << job.first << " | Command: " << job.second << endl;
}

// Bring a background job to foreground
void bringToForeground(pid_t pid) {
    if (jobs.find(pid) == jobs.end()) {
        cout << "No such job.\n";
        return;
    }
    cout << "Bringing PID " << pid << " to foreground...\n";
    waitpid(pid, NULL, 0);
    jobs.erase(pid);
}

// Main shell loop
int main() {
    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, handle_sigtstp);

    string input;
    while (true) {
        cout << "AyushShell$ ";
        getline(cin, input);

        if (input.empty()) continue;
        if (input == "exit") break;

        bool background = false;
        if (input.find('&') != string::npos) {
            background = true;
            input.erase(input.find('&'));
        }

        vector<string> tokens = tokenize(input);
        if (tokens.empty()) continue;

        // Built-in commands
        if (tokens[0] == "cd") {
            if (tokens.size() > 1) chdir(tokens[1].c_str());
            else cout << "Usage: cd <directory>\n";
            continue;
        } else if (tokens[0] == "jobs") {
            listJobs();
            continue;
        } else if (tokens[0] == "fg") {
            if (tokens.size() > 1) bringToForeground(stoi(tokens[1]));
            else cout << "Usage: fg <pid>\n";
            continue;
        }

        if (find(tokens.begin(), tokens.end(), string("|")) != tokens.end())
            handlePipes(tokens, background);
        else if (find(tokens.begin(), tokens.end(), string(">")) != tokens.end() ||
                 find(tokens.begin(), tokens.end(), string("<")) != tokens.end())
            handleRedirection(tokens, background);
        else
            executeCommand(tokens, background);
    }

    cout << "Exiting AyushShell...\n";
    return 0;
}
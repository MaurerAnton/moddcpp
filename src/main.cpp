// moddcpp - C++ file watcher + process runner (port of modd)
// Uses inotify (Linux) for file watching, runs commands on changes.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

static const char* VERSION = "0.1.0";
static std::atomic<bool> running(true);
static pid_t childPid = 0;

static void sigHandler(int) {
    running = false;
    if (childPid > 0) kill(childPid, SIGTERM);
}

struct Rule {
    std::string match;       /* glob pattern for files */
    std::vector<std::string> commands; /* commands to run */
    bool negate = false;     /* ! prefix */
};

struct Config {
    std::vector<std::string> watchDirs;
    std::vector<Rule> rules;
    int throttleMs = 500;
};

/* Parse config file (simple key-value + block format) */
static Config parseConfig(const std::string& path) {
    Config c;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "Cannot open config: %s\n", path.c_str()); exit(1); }

    char line[1024];
    Rule curRule;
    bool inRule = false;

    while (fgets(line, sizeof(line), f)) {
        /* Trim */
        char* s = line; while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s); while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) s[--len] = 0;

        if (s[0] == '#' || s[0] == 0) continue;

        /* Watch directive */
        if (strncmp(s, "**/*", 4) == 0 || s[0] == '/' || s[0] == '.') {
            if (inRule) { c.rules.push_back(curRule); curRule = Rule(); inRule = false; }
            c.watchDirs.push_back(s);
        }
        /* Command */
        else if (s[0] == '@' || strncmp(s, "go ", 3) == 0 || strncmp(s, "make", 4) == 0 ||
                  strncmp(s, "echo", 4) == 0 || strncmp(s, "./", 2) == 0 || s[0] == '{') {
            if (!inRule) { curRule.match = "**/*"; inRule = true; }
            curRule.commands.push_back(s);
        }
        /* Pattern with command block */
        else if (s[0] == '!' || strchr(s, '*')) {
            if (inRule) c.rules.push_back(curRule);
            curRule = Rule();
            curRule.match = s;
            if (s[0] == '!') { curRule.negate = true; curRule.match = s + 1; }
            inRule = true;
        }
    }
    if (inRule) c.rules.push_back(curRule);
    fclose(f);

    /* Default: watch current dir */
    if (c.watchDirs.empty()) c.watchDirs.push_back(".");
    /* Default rule: run anything that changed */
    if (c.rules.empty()) c.rules.push_back({"**/*", {"echo 'file changed'"}});

    return c;
}

/* Collect all files matching a pattern in a directory */
static std::vector<std::string> findFiles(const std::string& dir, const std::string& pattern) {
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) return files;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue; /* skip hidden */
        std::string full = dir + "/" + entry->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            auto sub = findFiles(full, pattern);
            files.insert(files.end(), sub.begin(), sub.end());
        } else if (S_ISREG(st.st_mode)) {
            if (fnmatch(pattern.c_str(), entry->d_name, FNM_PATHNAME) == 0 ||
                fnmatch(pattern.c_str(), full.c_str(), FNM_PATHNAME) == 0) {
                files.push_back(full);
            }
        }
    }
    closedir(d);
    return files;
}

/* Execute a command, return exit code */
static int runCommand(const std::string& cmd) {
    printf("\033[1;33m>>> %s\033[0m\n", cmd.c_str());
    fflush(stdout);

    childPid = fork();
    if (childPid == 0) {
        /* Child: run command via shell */
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    int status;
    waitpid(childPid, &status, 0);
    childPid = 0;

    if (WIFEXITED(status)) {
        int rc = WEXITSTATUS(status);
        if (rc != 0) printf("\033[31mCommand failed (exit %d)\033[0m\n", rc);
        return rc;
    }
    return -1;
}

/* Execute all matching rules */
static void executeRules(const Config& cfg, const std::string& changedFile) {
    for (auto& rule : cfg.rules) {
        bool match = false;
        for (auto& dir : cfg.watchDirs) {
            if (fnmatch(rule.match.c_str(), changedFile.c_str(), FNM_PATHNAME) == 0) {
                match = true; break;
            }
        }
        if (rule.negate) match = !match;
        if (match) {
            for (auto& cmd : rule.commands) runCommand(cmd);
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "moddcpp %s - file watcher + process runner\n", VERSION);
        fprintf(stderr, "Usage: moddcpp [config]\n");
        return 1;
    }

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    Config cfg = parseConfig(argv[1]);
    printf("moddcpp %s - watching %zu dirs, %zu rules\n", VERSION, cfg.watchDirs.size(), cfg.rules.size());

    /* Set up inotify watches */
    int inot = inotify_init1(IN_NONBLOCK);
    if (inot < 0) { perror("inotify_init"); return 1; }

    std::vector<int> wds;
    for (auto& dir : cfg.watchDirs) {
        int wd = inotify_add_watch(inot, dir.c_str(),
            IN_MODIFY | IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_TO);
        if (wd < 0) {
            fprintf(stderr, "Cannot watch: %s\n", dir.c_str());
        } else {
            wds.push_back(wd);
            printf("Watching: %s\n", dir.c_str());
        }
    }

    if (wds.empty()) { fprintf(stderr, "No directories to watch\n"); return 1; }

    /* Main loop: poll inotify events */
    char buf[4096];
    auto lastEvent = std::chrono::steady_clock::now();

    while (running) {
        ssize_t n = read(inot, buf, sizeof(buf));
        if (n > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEvent).count();

            /* Throttle: don't react too fast */
            if (elapsed >= cfg.throttleMs) {
                lastEvent = now;

                /* Parse events */
                ssize_t pos = 0;
                while (pos < n) {
                    struct inotify_event* ev = (struct inotify_event*)(buf + pos);
                    if (ev->len > 0) {
                        std::string name = ev->name;
                        /* Find parent dir */
                        for (size_t i = 0; i < cfg.watchDirs.size(); i++) {
                            if ((size_t)wds[i] == ev->wd) {
                                std::string full = cfg.watchDirs[i] + "/" + name;
                                printf("\033[90m[%s]\033[0m\n", full.c_str());
                                executeRules(cfg, full);
                                break;
                            }
                        }
                    }
                    pos += sizeof(struct inotify_event) + ev->len;
                }
            }
        }
        usleep(100000); /* 100ms poll interval */
    }

    printf("\nShutting down...\n");
    for (int wd : wds) inotify_rm_watch(inot, wd);
    close(inot);
    return 0;
}

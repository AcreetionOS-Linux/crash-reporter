#ifndef CRASH_REPORTER_H
#define CRASH_REPORTER_H

#include <sys/utsname.h>

// Structure to hold system information
typedef struct {
    char* hostname;
    char* kernel;
    char* os_release;
    char* uptime;
    char* pacman_log_errors;
    char* journalctl_errors;
    char* dmesg_errors;
} SystemInfo;

// Function to free SystemInfo memory
void free_system_info(SystemInfo* info);

// Function declarations
char* get_hostname();
char* get_kernel_version();
char* get_os_release();
char* get_uptime();
char* get_pacman_log_errors();
char* get_journalctl_errors();
char* get_dmesg_errors();
int detect_errors(const char* text);
// Creates a GitHub issue and returns an allocated string containing the issue URL (html_url) on success.
// Caller must free() the returned string. Returns NULL on failure.
char* create_github_issue(const char* title, const char* body);
char* generate_ai_message(const char* system_info_json);

// Runtime API key management (set at runtime from GUI or loaded from disk)
void set_runtime_github_token(const char* token);
void set_runtime_gemini_api_key(const char* key);
void load_runtime_keys(void);
void save_runtime_keys(const char* github_token, const char* gemini_key);

// Accessors used internally
const char* get_effective_github_token(void);
const char* get_effective_gemini_key(void);

// Access runtime-only stored tokens (NULL if not set)
const char* get_runtime_github_token(void);
const char* get_runtime_gemini_key(void);

// Gather and format all system errors into a single allocated string. Caller must free.
char* gather_all_errors(SystemInfo* info);
// Show four explanatory dialogs to the user before any privilege escalation.
// This should be called once at startup (after GTK is initialized).
void show_escalation_explanation_dialogs(void);
// Pre-authenticate polkit (perform a probe so the auth agent prompts once).
void preauthenticate_polkit(void);

#endif // CRASH_REPORTER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <curl/curl.h>
#include <jansson.h>
#include "config.h"
#include "crash_reporter_gui.h"
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <fcntl.h>

// File-scope flag controlling whether polkit has been authenticated for this run.
static int polkit_authenticated = 0;

// Forward declaration for helper used before actual definition
char* execute_command(const char* cmd);

// Trigger a one-time pkexec probe so the polkit agent prompts now (if needed).
// This helps ensure the user only types their password once after seeing explanations.
void preauthenticate_polkit(void) {
    if (geteuid() == 0) return; // already root
    if (polkit_authenticated) return;

    const char *pkexec_paths[] = {"/usr/bin/pkexec", "/bin/pkexec", NULL};
    const char *pkexec = NULL;
    for (int i = 0; pkexec_paths[i]; ++i) {
        if (access(pkexec_paths[i], X_OK) == 0) { pkexec = pkexec_paths[i]; break; }
    }
    if (!pkexec) return;

    char probe_cmd[256];
    snprintf(probe_cmd, sizeof(probe_cmd), "%s /bin/sh -c 'echo POLKIT_OK'", pkexec);
    char *probe_out = execute_command(probe_cmd);
    if (probe_out && strstr(probe_out, "POLKIT_OK") != NULL) {
        polkit_authenticated = 1;
    }
    if (probe_out) free(probe_out);
}

// Function to free SystemInfo memory
void free_system_info(SystemInfo* info) {
    free(info->hostname);
    free(info->kernel);
    free(info->os_release);
    free(info->uptime);
    free(info->pacman_log_errors);
    free(info->journalctl_errors);
    free(info->dmesg_errors);
}


// Function declarations
char* get_hostname();
char* get_kernel_version();
char* get_os_release();
char* get_uptime();
char* get_pacman_log_errors();
char* get_journalctl_errors();
char* get_dmesg_errors();
int detect_errors(const char* text);
char* create_github_issue(const char* title, const char* body);
char* generate_ai_message(const char* system_info_json);

// A helper function to execute shell commands and return output
char* execute_command(const char* cmd) {
    FILE *fp;
    char path[1024];
    char* result = strdup(""); // Initialize with an empty string
    size_t total_len = 0;

    // Open the command for reading.
    fp = popen(cmd, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to run command: %s\n", cmd);
        free(result);
        return strdup("Error: Command failed to execute");
    }

    // Read the output a line at a time - output it.
    while (fgets(path, sizeof(path), fp) != NULL) {
        size_t line_len = strlen(path);
        // Check for potential realloc failure before reallocating
        char* new_result = realloc(result, total_len + line_len + 1);
        if (new_result == NULL) {
            perror("realloc failed in execute_command");
            free(result);
            pclose(fp);
            return strdup("Error: Memory allocation failure");
        }
        result = new_result;
        strcpy(result + total_len, path);
        total_len += line_len;
    }
    if (result != NULL) {
        result[total_len] = '\0'; // Null-terminate the string
    }

    pclose(fp);
    return result;
}

// Escape single quotes in a shell argument for inclusion inside single quotes
static char* escape_single_quotes(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    // worst case every char is a quote, need 4x space roughly, be generous
    size_t bufsize = len * 4 + 1;
    char *out = malloc(bufsize);
    if (!out) return NULL;
    char *p = out;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '\'') {
            // close quote, insert escaped sequence '\'' and reopen
            strcpy(p, "'\\''");
            p += 4;
        } else {
            *p++ = s[i];
        }
    }
    *p = '\0';
    return out;
}

// Execute a command with polkit (pkexec) when not root. Returns allocated string like execute_command.
char* execute_privileged_command(const char* cmd) {
    if (geteuid() == 0) {
        return execute_command(cmd);
    }

    // (Pre-escalation explanatory dialogs are shown from the GUI at startup.)

    // Try pkexec path
    const char *pkexec_paths[] = {"/usr/bin/pkexec", "/bin/pkexec", NULL};
    const char *pkexec = NULL;
    for (int i = 0; pkexec_paths[i]; ++i) {
        if (access(pkexec_paths[i], X_OK) == 0) { pkexec = pkexec_paths[i]; break; }
    }
    if (!pkexec) {
        // fallback: attempt normal command (will likely fail for privileged files)
        return execute_command(cmd);
    }

    // If not authenticated yet, run a lightweight pkexec probe (this may prompt once).
    if (!polkit_authenticated) {
        char probe_cmd[256];
        snprintf(probe_cmd, sizeof(probe_cmd), "%s /bin/sh -c 'echo POLKIT_OK'", pkexec);
        char *probe_out = execute_command(probe_cmd);
        if (probe_out && strstr(probe_out, "POLKIT_OK") != NULL) {
            polkit_authenticated = 1;
        }
        if (probe_out) free(probe_out);
    }

    char *escaped = escape_single_quotes(cmd);
    if (!escaped) return execute_command(cmd);

    // Build: pkexec /bin/sh -c 'escaped' 2>&1
    size_t full_len = strlen(pkexec) + strlen(" /bin/sh -c '' 2>&1") + strlen(escaped) + 1;
    char *fullcmd = malloc(full_len);
    if (!fullcmd) { free(escaped); return execute_command(cmd); }
    snprintf(fullcmd, full_len, "%s /bin/sh -c '%s' 2>&1", pkexec, escaped);

    char *res = execute_command(fullcmd);
    free(escaped);
    free(fullcmd);
    return res;
}

char* get_hostname() {
    char *out = NULL;

    // Try absolute path first only if it exists
    if (access("/bin/hostname", X_OK) == 0) {
        out = execute_command("/bin/hostname");
    }
    if (out == NULL || strlen(out) == 0) {
        out = execute_command("hostname");
    }

    // If still empty or NULL, try reading /etc/hostname
    if (out == NULL || strlen(out) == 0) {
        if (out) free(out);
        FILE *f = fopen("/etc/hostname", "r");
        if (f) {
            char buf[256];
            if (fgets(buf, sizeof(buf), f) != NULL) {
                // Trim newline
                buf[strcspn(buf, "\n")] = '\0';
                fclose(f);
                return strdup(buf);
            }
            fclose(f);
        }
        return strdup("unknown");
    }

    // Trim trailing newline
    out[strcspn(out, "\n")] = '\0';
    return out;
}

char* get_kernel_version() {
    struct utsname buffer;
    if (uname(&buffer) != 0) {
        perror("uname");
        return strdup("Error getting kernel version");
    }
    return strdup(buffer.release);
}

char* get_os_release() {
    // Reading from /etc/os-release directly
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp == NULL) {
        perror("Error opening /etc/os-release");
        return strdup("Error reading OS release");
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    char* os_release_info = NULL;
    size_t total_len = 0;

    while ((read = getline(&line, &len, fp)) != -1) {
        os_release_info = (char*)realloc(os_release_info, total_len + read + 1);
        if (os_release_info == NULL) {
            perror("realloc failed");
            free(line);
            fclose(fp);
            return strdup("Error reading OS release");
        }
        strcpy(os_release_info + total_len, line);
        total_len += read;
    }
    if (os_release_info != NULL) {
        os_release_info[total_len] = '\0';
    }

    free(line);
    fclose(fp);
    return os_release_info;
}

char* get_uptime() {
    return execute_command("uptime");
}

char* get_pacman_log_errors() {
    return execute_privileged_command("grep -i \"error\" /var/log/pacman.log");
}

char* get_journalctl_errors() {
    // journalctl can require privileges for some logs; use polkit if available
    return execute_privileged_command("journalctl -b -p err..warning --no-pager");
}

char* get_dmesg_errors() {
    // dmesg often requires elevated privileges; try polkit first
    char* output = execute_privileged_command("dmesg --level=err,warn");
    if (output == NULL) {
        return execute_command("dmesg");
    }
    // if pkexec not available it may have returned the same error text; fallback
    if (strstr(output, "not found") != NULL || strstr(output, "Operation not permitted") != NULL) {
        free(output);
        return execute_command("dmesg");
    }
    return output;
}

int detect_errors(const char* text) {
    const char* error_keywords[] = {"error", "fail", "warn", "critical"};
    int num_keywords = sizeof(error_keywords) / sizeof(error_keywords[0]);

    for (int i = 0; i < num_keywords; i++) {
        if (strstr(text, error_keywords[i]) != NULL) {
            return 1; // True, error detected
        }
    }
    return 0; // False, no error detected
}

// Helper to append a section into a growing buffer with per-section truncation
static void append_section_with_limit(char **out_buf, size_t *out_len, size_t *out_cap, const char *title, const char *content, size_t section_limit) {
    if (!title) title = "";
    if (!content) content = "(no data)";

    size_t title_len = strlen(title);
    size_t content_len = strlen(content);
    // compute added size (with separators)
    size_t add = title_len + 4 + (content_len > section_limit ? section_limit + 32 : content_len) + 4;
    if (*out_len + add + 1 > *out_cap) {
        size_t newcap = (*out_cap == 0) ? (add + 1024) : (*out_cap * 2 + add);
        char *n = realloc(*out_buf, newcap);
        if (!n) return;
        *out_buf = n;
        *out_cap = newcap;
    }

    // Append header
    size_t p = *out_len;
    int wrote = snprintf(*out_buf + p, *out_cap - p, "== %s ==\n", title);
    if (wrote < 0) return;
    p += wrote;

    // Append content, possibly truncated
    if (content_len > section_limit) {
        // copy head portion and add truncation note
        memcpy(*out_buf + p, content, section_limit);
        p += section_limit;
        const char *note = "\n... (truncated)\n";
        size_t nl = strlen(note);
        memcpy(*out_buf + p, note, nl);
        p += nl;
    } else {
        memcpy(*out_buf + p, content, content_len);
        p += content_len;
    }

    // trailing newline
    (*out_buf)[p++] = '\n';
    (*out_buf)[p] = '\0';
    *out_len = p;
}

// Gather and format errors from multiple sources. Limits each section to ~200KB by default.
char* gather_all_errors(SystemInfo* info) {
    const size_t SECTION_LIMIT = 200 * 1024; // 200KB per section
    char *buffer = NULL;
    size_t buflen = 0, bufcap = 0;

    // 1) Basic metadata header
    char meta[1024];
    snprintf(meta, sizeof(meta), "Hostname: %s\nKernel: %s\nOS Release: %s\nUptime: %s\n\n",
             info && info->hostname ? info->hostname : "(unknown)",
             info && info->kernel ? info->kernel : "(unknown)",
             info && info->os_release ? info->os_release : "(unknown)",
             info && info->uptime ? info->uptime : "(unknown)");
    append_section_with_limit(&buffer, &buflen, &bufcap, "System Metadata", meta, SECTION_LIMIT);

    // 2) Failed systemd units
    char *svc = execute_command("systemctl --failed --no-legend --no-pager 2>/dev/null || true");
    append_section_with_limit(&buffer, &buflen, &bufcap, "Systemd Failed Units", svc ? svc : "(none)", SECTION_LIMIT);
    if (svc) free(svc);

    // 3) Journalctl errors (all time)
    char *journal = execute_privileged_command("journalctl -p err..emerg --no-pager 2>/dev/null || true");
    append_section_with_limit(&buffer, &buflen, &bufcap, "Journalctl (errors)", journal ? journal : "(none)", SECTION_LIMIT);
    if (journal) free(journal);

    // 4) Dmesg errors/warnings
    char *dmesg = execute_privileged_command("dmesg --level=err,warn 2>/dev/null || true");
    append_section_with_limit(&buffer, &buflen, &bufcap, "Kernel dmesg (err,warn)", dmesg ? dmesg : "(none)", SECTION_LIMIT);
    if (dmesg) free(dmesg);

    // 5) Pacman log errors
    char *pac = execute_privileged_command("grep -I -n -i \"error\" /var/log/pacman.log 2>/dev/null || true");
    append_section_with_limit(&buffer, &buflen, &bufcap, "Pacman Log Errors", pac ? pac : "(none)", SECTION_LIMIT);
    if (pac) free(pac);

    // 6) Grep /var/log for 'error' across many logs (limit search depth)
    char *others = execute_privileged_command("find /var/log -type f -maxdepth 3 -readable -exec grep -I -n -i \"error\" {} + 2>/dev/null || true");
    append_section_with_limit(&buffer, &buflen, &bufcap, "Other /var/log Matches (grep -i 'error')", others ? others : "(none)", SECTION_LIMIT);
    if (others) free(others);

    // 7) Any systemctl status messages for failed units (verbose)
    char *unit_list = execute_command("systemctl --failed --no-legend --no-pager | awk '{print $1}' 2>/dev/null || true");
    if (unit_list && strlen(unit_list) > 0) {
        // For each unit (space/newline separated), gather 'systemctl status' (limit)
        append_section_with_limit(&buffer, &buflen, &bufcap, "Failed Unit Statuses (truncated)", "Collecting unit statuses...", SECTION_LIMIT);
        // We'll run a safe loop and append statuses; to avoid shell quoting issues, write to a temp file and read lines
        char tmpf[] = "/tmp/crashrep_units_XXXXXX";
        int fd = mkstemp(tmpf);
        if (fd >= 0) {
            write(fd, unit_list, strlen(unit_list));
            close(fd);
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "while read u; do systemctl status --no-pager --full $u 2>/dev/null || true; echo \"---\"; done < %s 2>/dev/null || true", tmpf);
            char *units_status = execute_privileged_command(cmd);
            if (units_status) {
                append_section_with_limit(&buffer, &buflen, &bufcap, "Detailed Failed Unit Statuses", units_status, SECTION_LIMIT);
                free(units_status);
            }
            unlink(tmpf);
        }
    }
    if (unit_list) free(unit_list);

    // If nothing was collected, produce a short note
    if (!buffer) {
        buffer = strdup("(no errors found or failed to collect error data)");
    }

    return buffer;
}

// Struct to hold response data from curl
struct MemoryStruct {
  char *memory;
  size_t size;
};

// Callback function for curl to write received data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
      /* out of memory! */
      printf("not enough memory (realloc returned NULL)\n");
      return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Runtime-stored API keys (set via GUI at runtime)
static char *runtime_github_token = NULL;
static char *runtime_gemini_key = NULL;

const char* get_effective_github_token(void) {
    if (runtime_github_token && strlen(runtime_github_token) > 0) return runtime_github_token;
    return GITHUB_TOKEN;
}

const char* get_effective_gemini_key(void) {
    if (runtime_gemini_key && strlen(runtime_gemini_key) > 0) return runtime_gemini_key;
    return GEMINI_API_KEY;
}

void set_runtime_github_token(const char* token) {
    if (runtime_github_token) { free(runtime_github_token); runtime_github_token = NULL; }
    if (token) runtime_github_token = strdup(token);
}

void set_runtime_gemini_api_key(const char* key) {
    if (runtime_gemini_key) { free(runtime_gemini_key); runtime_gemini_key = NULL; }
    if (key) runtime_gemini_key = strdup(key);
}

const char* get_runtime_github_token(void) {
    return runtime_github_token;
}

const char* get_runtime_gemini_key(void) {
    return runtime_gemini_key;
}

// Save to XDG config file (~/.config/crash-reporter/keys.json)
void save_runtime_keys(const char* github_token, const char* gemini_key) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char path[PATH_MAX];
    if (!xdg) {
        const char *home = getenv("HOME");
        if (!home) return;
        snprintf(path, sizeof(path), "%s/.config/crash-reporter", home);
    } else {
        snprintf(path, sizeof(path), "%s/crash-reporter", xdg);
    }
    // create dir if needed
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0700);
    }
    char file[PATH_MAX];
    snprintf(file, sizeof(file), "%s/keys.json", path);

    json_t *root = json_object();
    if (github_token) json_object_set_new(root, "github_token", json_string(github_token));
    if (gemini_key) json_object_set_new(root, "gemini_api_key", json_string(gemini_key));

    char *data = json_dumps(root, JSON_INDENT(2));
    // write atomically
    char tmpfile[PATH_MAX];
    snprintf(tmpfile, sizeof(tmpfile), "%s/keys.json.tmp", path);
    int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        write(fd, data, strlen(data));
        fsync(fd);
        close(fd);
        rename(tmpfile, file);
    }
    free(data);
    json_decref(root);
}

// Load keys from disk (if any)
void load_runtime_keys(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char file[PATH_MAX];
    if (!xdg) {
        const char *home = getenv("HOME");
        if (!home) return;
        snprintf(file, sizeof(file), "%s/.config/crash-reporter/keys.json", home);
    } else {
        snprintf(file, sizeof(file), "%s/crash-reporter/keys.json", xdg);
    }
    FILE *f = fopen(file, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    json_error_t err;
    json_t *root = json_loads(buf, 0, &err);
    free(buf);
    if (!root) return;
    json_t *g = json_object_get(root, "github_token");
    if (json_is_string(g)) set_runtime_github_token(json_string_value(g));
    json_t *gg = json_object_get(root, "gemini_api_key");
    if (json_is_string(gg)) set_runtime_gemini_api_key(json_string_value(gg));
    json_decref(root);
}

char* create_github_issue(const char* title, const char* body) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    json_t *root;
    char *json_data;
    char auth_header[256];
    struct curl_slist *headers = NULL;
    char url[512];
    char *created_url = NULL;

    chunk.memory = malloc(1);  /* will be grown as needed by realloc */
    chunk.size = 0;    /* no data at this point */

    const char *effective_token = get_effective_github_token();
    if (!effective_token || strcmp(effective_token, "your_github_token_here") == 0) {
        fprintf(stderr, "GitHub token not configured. Please set it via the GUI or edit src/config.h.\n");
        fprintf(stderr, "Generate a Personal Access Token from GitHub settings with 'repo' scope for creating issues.\n");
        free(chunk.memory);
        return NULL;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if(curl) {
        snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/issues", GITHUB_REPO_OWNER, GITHUB_REPO_NAME);
        curl_easy_setopt(curl, CURLOPT_URL, url);

        snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", effective_token);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "User-Agent: AcreetionOS-Crash-Reporter");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Construct JSON payload
        root = json_object();
        json_object_set_new(root, "title", json_string(title));
        json_object_set_new(root, "body", json_string(body));

        json_data = json_dumps(root, 0);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("GitHub API response: %s\n", chunk.memory);
            json_t *response_root = json_loads(chunk.memory, 0, NULL);
            if (response_root) {
                json_t *html_url_obj = json_object_get(response_root, "html_url");
                if (json_is_string(html_url_obj)) {
                    const char *urlstr = json_string_value(html_url_obj);
                    printf("GitHub issue created: %s\n", urlstr);
                    created_url = strdup(urlstr);
                } else {
                    printf("Failed to get issue URL from response.\n");
                }
                json_decref(response_root);
            } else {
                printf("Failed to parse GitHub API response.\n");
            }
        }

        json_decref(root);
        free(json_data);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    free(chunk.memory);
    curl_global_cleanup();
    return created_url; // may be NULL on failure
}

char* generate_ai_message(const char* system_info_json) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    json_t *root, *contents_array, *part_object, *text_object, *candidate_array, *content_object, *ai_text_object;
    char *json_data;
    struct curl_slist *headers = NULL;
    char url[512];
    char* ai_message = NULL;

    chunk.memory = malloc(1);  /* will be grown as needed by realloc */
    chunk.size = 0;    /* no data at this point */

    const char *effective_gemini = get_effective_gemini_key();
    if (!effective_gemini || strcmp(effective_gemini, "your_gemini_api_key_here") == 0) {
        fprintf(stderr, "Gemini API key not configured. Please set it via the GUI or edit src/config.h.\n");
        fprintf(stderr, "Obtain your Gemini API key from Google AI Studio: https://aistudio.google.com/, click 'Get API key'.\n");
        free(chunk.memory);
        return strdup("AI message generation skipped due to missing API key.");
    }

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if(curl) {
        snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1/models/gemini-pro:generateContent?key=%s", effective_gemini);
        curl_easy_setopt(curl, CURLOPT_URL, url);

        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Construct JSON payload for Gemini API
        root = json_object();
        contents_array = json_array();
        part_object = json_object();
        text_object = json_string(system_info_json);
        json_object_set_new(part_object, "text", text_object);
        json_array_append_new(contents_array, part_object);
        json_object_set_new(root, "contents", contents_array);

        json_data = json_dumps(root, 0);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            ai_message = strdup("Error generating AI message");
        } else {
            printf("Gemini API response: %s\n", chunk.memory);
            json_t *response_root = json_loads(chunk.memory, 0, NULL);
            if (response_root) {
                candidate_array = json_object_get(response_root, "candidates");
                if (json_is_array(candidate_array) && json_array_size(candidate_array) > 0) {
                    content_object = json_object_get(json_array_get(candidate_array, 0), "content");
                    if (content_object) {
                        ai_text_object = json_object_get(content_object, "text");
                        if (json_is_string(ai_text_object)) {
                            ai_message = strdup(json_string_value(ai_text_object));
                        }
                    }
                }
                json_decref(response_root);
            }
            if (ai_message == NULL) {
                ai_message = strdup("Failed to parse AI message from response.");
            }
        }

        json_decref(root);
        free(json_data);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    free(chunk.memory);
    curl_global_cleanup();

    return ai_message;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    SystemInfo info = {0};

    // Load any saved runtime API keys from disk
    load_runtime_keys();

    // Collect only non-privileged metadata now. Privileged collections (journalctl, dmesg,
    // pacman logs) will be performed after the GUI shows the explanation page and we
    // preauthenticate polkit so the user is prompted only once.
    info.hostname = get_hostname();
    info.kernel = get_kernel_version();
    info.os_release = get_os_release();
    info.uptime = get_uptime();
    info.pacman_log_errors = NULL;
    info.journalctl_errors = NULL;
    info.dmesg_errors = NULL;

    create_and_show_gui(argc, argv, &info);

    free_system_info(&info);

    return 0;
}

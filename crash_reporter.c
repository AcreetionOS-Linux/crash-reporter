#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <curl/curl.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#define DISCORD_WEBHOOK_URL "https://discord.com/api/webhooks/1440171277477220383/YQo1Wg2mD_A_39Ikw_cmjOP90pUXIPLd4BK0svnALOpvsMqZZ3mIgiFtDuUjDNH4MHRI"

// Structure to hold report data
typedef struct {
    char* backtrace;
    char* system_info;
    char* user_notes;
} CrashReport;

// Function prototypes
static void send_report(const CrashReport* report);
static char* get_backtrace();
static char* get_system_info();
static void show_crash_dialog();
void crash_handler(int sig);
void cause_crash();

// UI Callbacks
static void on_yes_clicked(GtkWidget *widget, gpointer data);
static void on_no_clicked(GtkWidget *widget, gpointer data);

static char* get_backtrace() {
    void* array[20];
    size_t size;
    char** strings;
    size_t i;
    GString* backtrace_str = g_string_new("");

    size = backtrace(array, 20);
    strings = backtrace_symbols(array, size);

    if (strings == NULL) {
        g_string_append(backtrace_str, "Could not get backtrace symbols.\n");
        return g_string_free(backtrace_str, FALSE);
    }

    for (i = 0; i < size; i++) {
        g_string_append_printf(backtrace_str, "%s\n", strings[i]);
    }

    free(strings);
    return g_string_free(backtrace_str, FALSE);
}

static char* get_system_info() {
    struct utsname sysinfo;
    if (uname(&sysinfo) != 0) {
        return g_strdup("Could not get system info.");
    }
    return g_strdup_printf("OS: %s %s\nArchitecture: %s", sysinfo.sysname, sysinfo.release, sysinfo.machine);
}

static void send_report(const CrashReport* report) {
    CURL *curl;
    CURLcode res;
    char* json_payload;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        json_payload = g_strdup_printf(
            "{\"content\": \"New Crash Report\","
            "\"embeds\": [{"
            "\"title\": \"Crash Report\","
            "\"fields\": ["
            "{\"name\": \"System Info\", \"value\": \"%s\"},"
            "{\"name\": \"User Notes\", \"value\": \"%s\"},"
            "{\"name\": \"Backtrace\", \"value\": \"```%s```\"}"
            "]}]}",
            report->system_info,
            report->user_notes,
            report->backtrace
        );

        curl_easy_setopt(curl, CURLOPT_URL, DISCORD_WEBHOOK_URL);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
        g_free(json_payload);
    }
    curl_global_cleanup();
}

static void on_yes_clicked(GtkWidget *widget, gpointer data) {
    GtkTextView* textview = GTK_TEXT_VIEW(data);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(textview);
    GtkTextIter start, end;
    
    CrashReport report;
    report.backtrace = get_backtrace();
    report.system_info = get_system_info();

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    report.user_notes = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    g_print("Yes button clicked! Sending report...\n");
    send_report(&report);

    g_free(report.backtrace);
    g_free(report.system_info);
    g_free(report.user_notes);

    gtk_main_quit();
}

static void on_no_clicked(GtkWidget *widget, gpointer data) {
    g_print("No button clicked. Report cancelled.\n");
    gtk_main_quit();
}

static void show_crash_dialog() {
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *yes_button;
    GtkWidget *no_button;
    GtkWidget *scrolled_window;
    GtkWidget *textview;
    GtkTextBuffer *buffer;

    gtk_init(NULL, NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Crash Report");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    g_signal_connect(window, "destroy", G_CALLBACK(on_no_clicked), NULL);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);

    label = gtk_label_new("An unexpected error occurred. Would you like to send a crash report to the developers?");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 2, 1);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    textview = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(buffer, "You can add optional comments here.", -1);
    gtk_container_add(GTK_CONTAINER(scrolled_window), textview);
    gtk_grid_attach(GTK_GRID(grid), scrolled_window, 0, 1, 2, 1);

    yes_button = gtk_button_new_with_label("Yes");
    g_signal_connect(yes_button, "clicked", G_CALLBACK(on_yes_clicked), textview);
    gtk_grid_attach(GTK_GRID(grid), yes_button, 0, 2, 1, 1);
    gtk_widget_set_hexpand(yes_button, TRUE);

    no_button = gtk_button_new_with_label("No");
    g_signal_connect(no_button, "clicked", G_CALLBACK(on_no_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), no_button, 1, 2, 1, 1);
    gtk_widget_set_hexpand(no_button, TRUE);

    gtk_widget_show_all(window);
    gtk_main();
}

void crash_handler(int sig) {
    pid_t pid = fork();
    if (pid == 0) { // Child process
        show_crash_dialog();
        exit(0);
    } else if (pid > 0) { // Parent process
        int status;
        waitpid(pid, &status, 0); // Wait for child to finish
        exit(1); // Exit the original crashed application
    } else { // Fork failed
        exit(1);
    }
}

void cause_crash() {
    char *ptr = NULL;
    *ptr = 'a'; // This will cause a segmentation fault
}

int main(int argc, char *argv[]) {
    // Install signal handler
    signal(SIGSEGV, crash_handler);

    g_print("Application started. Crashing in 3 seconds...\n");
    sleep(3);
    cause_crash();

    // The code below will not be reached
    return 0;
}

/* Clean GUI implementation for crash reporter
 * Single set of callbacks, CSS loaded only if present, GTK init is called in main()
 */

#include <gtk/gtk.h>
#include <string.h>
#include <unistd.h> // geteuid
#include "crash_reporter_gui.h"
#include "config.h"
#include "crash_reporter.h"
#include "crash_reporter.h" // for set_runtime_* and save/load

typedef struct {
    SystemInfo *info;
    GtkEntry *egh;
    GtkEntry *egm;
    GtkToggleButton *save;
} GUIContext;

// Callback for GitHub Token button
void on_github_token_button_clicked(GtkButton *button, gpointer user_data) {
    g_app_info_launch_default_for_uri("https://github.com/settings/tokens/new?scopes=repo&description=AcreetionOS_Crash_Reporter_Token", NULL, NULL);
}

// Callback for Gemini API Key button
void on_gemini_api_key_button_clicked(GtkButton *button, gpointer user_data) {
    g_app_info_launch_default_for_uri("https://aistudio.google.com/app/apikey", NULL, NULL);
}

// Forward declaration for report handler used by file button
void on_report_bug_button_clicked(GtkButton *button, gpointer user_data);

// Show a dialog to enter GitHub and Gemini API keys and optionally save them
void on_set_api_keys_clicked(GtkButton *button, gpointer user_data) {
    SystemInfo *info = (SystemInfo*)user_data; (void)info;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Set API Keys", NULL, GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, 200);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);

    GtkWidget *lbl1 = gtk_label_new("GitHub Token:");
    gtk_widget_set_halign(lbl1, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl1, 0, 0, 1, 1);
    GtkWidget *entry_github = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry_github), TRUE);
    gtk_grid_attach(GTK_GRID(grid), entry_github, 1, 0, 1, 1);

    GtkWidget *lbl2 = gtk_label_new("Gemini API Key:");
    gtk_widget_set_halign(lbl2, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl2, 0, 1, 1, 1);
    GtkWidget *entry_gemini = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry_gemini), TRUE);
    gtk_grid_attach(GTK_GRID(grid), entry_gemini, 1, 1, 1, 1);

    GtkWidget *save_chk = gtk_check_button_new_with_label("Save to disk (stored with 0600 permissions)");
    gtk_grid_attach(GTK_GRID(grid), save_chk, 0, 2, 2, 1);

    // Prefill entries if runtime keys exist and pre-check save if saved values are present
    const char *rgh = get_runtime_github_token();
    const char *rgm = get_runtime_gemini_key();
    if (rgh && rgh[0]) {
        gtk_entry_set_text(GTK_ENTRY(entry_github), rgh);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(save_chk), TRUE);
    }
    if (rgm && rgm[0]) {
        gtk_entry_set_text(GTK_ENTRY(entry_gemini), rgm);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(save_chk), TRUE);
    }

    gtk_widget_show_all(dialog);

    int res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        const char *g_tok = gtk_entry_get_text(GTK_ENTRY(entry_github));
        const char *gm_tok = gtk_entry_get_text(GTK_ENTRY(entry_gemini));
        // set runtime tokens
        set_runtime_github_token(g_tok && g_tok[0] ? g_tok : NULL);
        set_runtime_gemini_api_key(gm_tok && gm_tok[0] ? gm_tok : NULL);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(save_chk))) {
            save_runtime_keys(g_tok && g_tok[0] ? g_tok : NULL, gm_tok && gm_tok[0] ? gm_tok : NULL);
        }
    }

    gtk_widget_destroy(dialog);
}

// Handler for the main right-side "File A Bug Report" button
void on_file_bug_button_clicked(GtkButton *button, gpointer user_data) {
    GUIContext *c = (GUIContext*)g_object_get_data(G_OBJECT(button), "gui-context");
    if (!c) {
        g_printerr("Internal GUI context missing\n");
        return;
    }
    const char *g_tok = gtk_entry_get_text(c->egh);
    const char *gm_tok = gtk_entry_get_text(c->egm);
    set_runtime_github_token(g_tok && g_tok[0] ? g_tok : NULL);
    set_runtime_gemini_api_key(gm_tok && gm_tok[0] ? gm_tok : NULL);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c->save))) {
        save_runtime_keys(g_tok && g_tok[0] ? g_tok : NULL, gm_tok && gm_tok[0] ? gm_tok : NULL);
    }
    // Trigger the same behavior as the report button
    on_report_bug_button_clicked(NULL, c->info);
}

// Callback for Report Bug button
void on_report_bug_button_clicked(GtkButton *button, gpointer user_data) {
    g_print("Report Bug button clicked!\n");
    SystemInfo* info = (SystemInfo*)user_data;

    // Gather a comprehensive, organized collection of errors from the system
    char *all_info = gather_all_errors(info);
    if (!all_info) {
        g_printerr("Failed to gather system errors\n");
        return;
    }

    if (detect_errors(all_info)) {
        g_print("Errors detected. Generating AI message and uploading to GitHub...\n");

        char* ai_message = generate_ai_message(all_info);
        if (ai_message == NULL) {
            g_printerr("Failed to generate AI message.\n");
            free(all_info);
            return;
        }

        char issue_title[256];
        snprintf(issue_title, sizeof(issue_title), "Automated Bug Report: System Errors Detected on %s", info->hostname ? info->hostname : "(unknown)");

        // GitHub limits issue body size (65536). Truncate parts if necessary.
        const size_t GITHUB_BODY_LIMIT = 65536;
        const char *header_fmt = "@%s\n\n## System Information\n```\n";
        const char *mid_fmt = "\n```\n\n## AI Generated Summary\n";
        const char *tail_fmt = "\n";

        size_t header_len = strlen(header_fmt) + strlen(GITHUB_PING_USERS);
        size_t mid_len = strlen(mid_fmt);
        size_t tail_len = strlen(tail_fmt);

        size_t available = (GITHUB_BODY_LIMIT > header_len + mid_len + tail_len) ? (GITHUB_BODY_LIMIT - header_len - mid_len - tail_len) : 0;

        // Split available roughly between system info and ai message
        size_t sys_allow = available / 2;
        size_t ai_allow = available - sys_allow;

        // Prepare truncated copies if necessary
        char *sys_part;
        const char *trunc_suffix = "\n... (truncated)";
        size_t suffix_len = strlen(trunc_suffix);
        if (strlen(all_info) > sys_allow) {
            size_t take = sys_allow > suffix_len ? sys_allow - suffix_len : 0;
            sys_part = malloc(take + suffix_len + 1);
            if (sys_part) {
                memcpy(sys_part, all_info, take);
                memcpy(sys_part + take, trunc_suffix, suffix_len);
                sys_part[take + suffix_len] = '\0';
            }
        } else {
            sys_part = strdup(all_info);
        }

        char *ai_part;
        if (strlen(ai_message) > ai_allow) {
            size_t take = ai_allow > suffix_len ? ai_allow - suffix_len : 0;
            ai_part = malloc(take + suffix_len + 1);
            if (ai_part) {
                memcpy(ai_part, ai_message, take);
                memcpy(ai_part + take, trunc_suffix, suffix_len);
                ai_part[take + suffix_len] = '\0';
            }
        } else {
            ai_part = strdup(ai_message);
        }

        size_t body_needed = header_len + strlen(sys_part) + mid_len + strlen(ai_part) + tail_len + 1;
        char *issue_body = (char*)malloc(body_needed);
        if (issue_body) {
            snprintf(issue_body, body_needed, "@%s\n\n## System Information\n```\n%s\n```\n\n## AI Generated Summary\n%s\n",
                     GITHUB_PING_USERS, sys_part, ai_part);
            char *issue_url = create_github_issue(issue_title, issue_body);
            if (issue_url) {
                // Show dialog with link and option to open in default browser
                gchar *msg = g_strdup_printf("GitHub issue created:\n%s", issue_url);
                GtkWidget *dlg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE, "%s", msg);
                gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Open in browser", 1, "_Copy link", 2, "_Close", GTK_RESPONSE_CLOSE, NULL);
                int resp = gtk_dialog_run(GTK_DIALOG(dlg));
                if (resp == 1) {
                    g_app_info_launch_default_for_uri(issue_url, NULL, NULL);
                } else if (resp == 2) {
                    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
                    gtk_clipboard_set_text(cb, issue_url, -1);
                    gtk_clipboard_store(cb);
                    GtkWidget *ok = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Issue URL copied to clipboard.");
                    gtk_dialog_run(GTK_DIALOG(ok));
                    gtk_widget_destroy(ok);
                }
                gtk_widget_destroy(dlg);
                g_free(msg);
                free(issue_url);
            }
            free(issue_body);
        } else {
            g_printerr("Failed to allocate memory for issue body\n");
        }
        free(sys_part);
        free(ai_part);

        free(ai_message);
    } else {
        g_print("No significant errors detected.\n");
    }
    free(all_info);
}

// Show four explanatory dialogs (called once at startup). If the user cancels any dialog,
// explain and continue without granting privileges; later privileged calls will simply
// attempt non-privileged fallbacks.
void show_escalation_explanation_dialogs(void) {
    const char *msgs[4] = {
        "This application needs elevated privileges to read kernel logs (dmesg) so it can identify hardware and driver errors.",
        "It will read system logs (journalctl) which may contain error messages from services. This helps locate failing units.",
        "It will read package manager logs (e.g. /var/log/pacman.log) to find installation or update errors.",
        "It will run 'systemctl status' on failed units to collect detailed failure traces.\n\nYou will be prompted for your password by the system authorization agent once."
    };
    for (int i = 0; i < 4; ++i) {
        GtkWidget *dlg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE, "%s", msgs[i]);
        gtk_window_set_title(GTK_WINDOW(dlg), "Why escalation is requested");
        gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Continue", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
        int resp = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (resp != GTK_RESPONSE_OK) {
            // User chose to cancel; show a brief notice and stop the sequence
            GtkWidget *note = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Privilege escalation will be skipped. The tool will attempt to collect available data without elevated privileges.");
            gtk_dialog_run(GTK_DIALOG(note));
            gtk_widget_destroy(note);
            return;
        }
    }
}

void create_and_show_gui(int argc, char *argv[], SystemInfo* info) {
    // At GUI startup, show explanatory dialogs about privilege escalation
    // so the user understands why we will request elevated access.
    show_escalation_explanation_dialogs();
    // After the user has seen the explanations, pre-authenticate polkit so
    // the authorization prompt will appear once (if needed) when we collect logs.
    preauthenticate_polkit();

    // Inform about privilege level but continue
    if (geteuid() != 0) {
        GtkWidget *dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "Root privileges recommended.\n\nThis application needs elevated privileges to read full system logs (like dmesg and pacman.log).\nTo collect full diagnostic information run: sudo ./crash_reporter\n\nIf you prefer not to run as root, the app will still show available information."
        );
        gtk_window_set_title(GTK_WINDOW(dialog), "Permission Notice");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

            // No application icon set (logo removed as requested)
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "AcreetionOS Crash Reporter");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Load CSS only if present
    if (g_file_test("src/style.css", G_FILE_TEST_EXISTS)) {
        GtkCssProvider *provider = gtk_css_provider_new();
        GError *err = NULL;
        gtk_css_provider_load_from_path(provider, "src/style.css", &err);
        if (err) {
            g_warning("Failed to load CSS: %s", err->message);
            g_clear_error(&err);
        } else {
            gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        }
        g_object_unref(provider);
    }

    // Main layout: left column with two webviews, right column with info and key form
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    // Left column: two placeholders (we open external pages in default browser)
    GtkWidget *left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(left_vbox, 600, 800);
    gtk_box_pack_start(GTK_BOX(hbox), left_vbox, FALSE, FALSE, 0);

    // Top placeholder for Gemini API page
    GtkWidget *top_frame = gtk_frame_new("Gemini API (opens in browser)");
    gtk_box_pack_start(GTK_BOX(left_vbox), top_frame, TRUE, TRUE, 0);
    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(top_frame), top_box);
    GtkWidget *top_label = gtk_label_new("Gemini API key page will open in your default browser.");
    gtk_box_pack_start(GTK_BOX(top_box), top_label, FALSE, FALSE, 0);
    GtkWidget *top_open_btn = gtk_button_new_with_label("Open Gemini API Page");
    gtk_box_pack_start(GTK_BOX(top_box), top_open_btn, FALSE, FALSE, 0);

    // Bottom placeholder for GitHub token page
    GtkWidget *bottom_frame = gtk_frame_new("GitHub Token (opens in browser)");
    gtk_box_pack_start(GTK_BOX(left_vbox), bottom_frame, TRUE, TRUE, 0);
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(bottom_frame), bottom_box);
    GtkWidget *bottom_label = gtk_label_new("GitHub token page will open in your default browser.");
    gtk_box_pack_start(GTK_BOX(bottom_box), bottom_label, FALSE, FALSE, 0);
    GtkWidget *bottom_open_btn = gtk_button_new_with_label("Open GitHub Token Page");
    gtk_box_pack_start(GTK_BOX(bottom_box), bottom_open_btn, FALSE, FALSE, 0);

    // Connect buttons to open the URLs in the default browser
    g_signal_connect(top_open_btn, "clicked", G_CALLBACK(on_gemini_api_key_button_clicked), NULL);
    g_signal_connect(bottom_open_btn, "clicked", G_CALLBACK(on_github_token_button_clicked), NULL);

    // Also open them once automatically (best-effort). Positioning of external browser windows cannot be controlled reliably.
    g_app_info_launch_default_for_uri("https://aistudio.google.com/app/apikey", NULL, NULL);
    g_app_info_launch_default_for_uri("https://github.com/settings/tokens/new?scopes=repo&description=AcreetionOS_Crash_Reporter_Token", NULL, NULL);

    // Right column: info and keys
    GtkWidget *right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(hbox), right_vbox, TRUE, TRUE, 0);

    // Header Bar: place inside right column for clarity
    GtkWidget *header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "AcreetionOS Crash Reporter");
    // Logo removed — header bar will not show an image
    gtk_box_pack_start(GTK_BOX(right_vbox), header_bar, FALSE, FALSE, 0);

    // System Information Section
    GtkWidget *label = gtk_label_new("<span size='12000' weight='bold'>System Information</span>");
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(right_vbox), label, FALSE, FALSE, 0);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    char buffer_text[8192];
    snprintf(buffer_text, sizeof(buffer_text),
             "Hostname: %s\nKernel: %s\nOS Release: %s\nUptime: %s\n\nPacman Log Errors:\n%s\n\nJournalctl Errors:\n%s\n\nDmesg Errors:\n%s",
             info->hostname ? info->hostname : "(none)", info->kernel ? info->kernel : "(none)", info->os_release ? info->os_release : "(none)", info->uptime ? info->uptime : "(none)",
             info->pacman_log_errors ? info->pacman_log_errors : "(none)", info->journalctl_errors ? info->journalctl_errors : "(none)", info->dmesg_errors ? info->dmesg_errors : "(none)");
    gtk_text_buffer_set_text(buffer, buffer_text, -1);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(right_vbox), scrolled_window, TRUE, TRUE, 0);

    // API key entry area on the right
    GtkWidget *keys_frame = gtk_frame_new("API Keys (enter once)");
    gtk_box_pack_start(GTK_BOX(right_vbox), keys_frame, FALSE, FALSE, 0);
    GtkWidget *keys_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(keys_frame), keys_box);

    GtkWidget *lbl_gh = gtk_label_new("GitHub Token:");
    gtk_widget_set_halign(lbl_gh, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(keys_box), lbl_gh, FALSE, FALSE, 0);
    GtkWidget *entry_gh = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(keys_box), entry_gh, FALSE, FALSE, 0);

    GtkWidget *lbl_gm = gtk_label_new("Gemini API Key:");
    gtk_widget_set_halign(lbl_gm, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(keys_box), lbl_gm, FALSE, FALSE, 0);
    GtkWidget *entry_gm = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(keys_box), entry_gm, FALSE, FALSE, 0);

    GtkWidget *save_chk = gtk_check_button_new_with_label("Save to disk (stored with 0600 permissions)");
    gtk_box_pack_start(GTK_BOX(keys_box), save_chk, FALSE, FALSE, 0);

    // Prefill entries from runtime keys
    const char *rgh = get_runtime_github_token();
    const char *rgm = get_runtime_gemini_key();
    if (rgh && rgh[0]) gtk_entry_set_text(GTK_ENTRY(entry_gh), rgh);
    if (rgm && rgm[0]) gtk_entry_set_text(GTK_ENTRY(entry_gm), rgm);
    if ((rgh && rgh[0]) || (rgm && rgm[0])) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(save_chk), TRUE);

    // File Bug button on the right
    GtkWidget *file_btn = gtk_button_new_with_label("File A Bug Report");
    gtk_box_pack_start(GTK_BOX(keys_box), file_btn, FALSE, FALSE, 0);

    // Wire file_btn to collect keys, set runtime tokens, optionally save, then trigger report
    typedef struct { SystemInfo *info; GtkEntry *egh; GtkEntry *egm; GtkToggleButton *save; } GUIContext;
    GUIContext *ctx = g_malloc0(sizeof(GUIContext));
    ctx->info = info;
    ctx->egh = GTK_ENTRY(entry_gh);
    ctx->egm = GTK_ENTRY(entry_gm);
    ctx->save = GTK_TOGGLE_BUTTON(save_chk);
    g_object_set_data_full(G_OBJECT(file_btn), "gui-context", ctx, (GDestroyNotify)g_free);

    g_signal_connect(file_btn, "clicked", G_CALLBACK(on_file_bug_button_clicked), NULL);

    // Show startup informational dialog explaining resources and steps
    GtkWidget *start = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "This tool will collect system logs and optionally create a GitHub issue. The services used (journalctl, dmesg, pacman logs) are local system resources and are free to read on your machine.\n\nSteps:\n1) Review the Gemini API page (left-top) and GitHub token page (left-bottom).\n2) Enter your API keys on the right and click 'File A Bug Report'.\n3) If necessary, authenticate the privilege prompt (polkit) that appears once.\n\nClick OK to continue and the pages will be shown in the left panes.");
    gtk_window_set_title(GTK_WINDOW(start), "About: Resource usage and steps");
    gtk_dialog_run(GTK_DIALOG(start));
    gtk_widget_destroy(start);

    // Prominent Set API Keys button (opens modal dialog)
    GtkWidget *set_keys_btn = gtk_button_new_with_label("Set API Keys...");
    gtk_widget_set_tooltip_text(set_keys_btn, "Enter your GitHub and Gemini API keys (saved if requested)");
    gtk_box_pack_start(GTK_BOX(right_vbox), set_keys_btn, FALSE, FALSE, 0);
    g_signal_connect(set_keys_btn, "clicked", G_CALLBACK(on_set_api_keys_clicked), info);

    // Populate system/error text view with organized errors (monospace)
    char *errors_all = gather_all_errors(info);
    if (errors_all) {
        gtk_text_buffer_set_text(buffer, errors_all, -1);
        free(errors_all);
    }
    // Use CSS to force a monospace font for better alignment in the text view
    gtk_widget_set_name(text_view, "system_text_view");
    GtkCssProvider *mono_provider = gtk_css_provider_new();
    const char *mono_css = "#system_text_view { font-family: monospace; font-size: 10pt; }";
    gtk_css_provider_load_from_data(mono_provider, mono_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(mono_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(mono_provider);

    gtk_widget_show_all(window);

    gtk_main();
}


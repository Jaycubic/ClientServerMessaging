#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cli.h"
#include "test_util.h"

static cli_result_t R;

static cli_kind_t parse(const char *in, int connected)
{
    cli_parse(in, connected, &R);
    return R.kind;
}

static void test_blank_and_help(void)
{
    CHECK(parse("", 0) == CLI_EMPTY);
    CHECK(parse("   \t ", 1) == CLI_EMPTY);
    CHECK(parse("help", 0) == CLI_HELP);
    CHECK(parse("HELP", 1) == CLI_HELP);
    CHECK(parse("?", 1) == CLI_HELP);
    CHECK(strstr(cli_help_text(), "sendmessage") != NULL);
}

static void test_connect(void)
{
    CHECK(parse("connect 127.0.0.1 alice", 0) == CLI_CONNECT);
    CHECK(strcmp(R.host, "127.0.0.1") == 0 && strcmp(R.user, "alice") == 0 && R.port == 0);
    CHECK(parse("  CONNECT   localhost   bob   6000 ", 0) == CLI_CONNECT);
    CHECK(strcmp(R.host, "localhost") == 0 && strcmp(R.user, "bob") == 0 && R.port == 6000);

    CHECK(parse("connect", 0) == CLI_ERROR && strstr(R.error, "Usage"));
    CHECK(parse("connect 127.0.0.1", 0) == CLI_ERROR);
    CHECK(parse("connect 127.0.0.1 bad|name", 0) == CLI_ERROR);
    CHECK(parse("connect 127.0.0.1 ../x", 0) == CLI_ERROR);
    CHECK(parse("connect 127.0.0.1 alice 0", 0) == CLI_ERROR);
    CHECK(parse("connect 127.0.0.1 alice 70000", 0) == CLI_ERROR);
    CHECK(parse("connect 127.0.0.1 alice abc", 0) == CLI_ERROR);
    CHECK(parse("connect 127.0.0.1 alice 5555", 1) == CLI_ERROR);   /* already connected */
    CHECK(strstr(R.error, "Already connected") != NULL);
}

static void test_requires_connection(void)
{
    const char *cmds[] = { "sendmessage bob hi", "getmessages bob", "deletemessages bob",
                           "getuserlist", "bogus" };
    for (size_t i = 0; i < sizeof cmds / sizeof *cmds; i++) {
        CHECK(parse(cmds[i], 0) == CLI_ERROR);
        CHECK(strstr(R.error, "Not connected") != NULL);
    }
    CHECK(parse("shutdown", 0) == CLI_SHUTDOWN);       /* allowed offline: just exit */
}

static void test_sendmessage(void)
{
    CHECK(parse("sendmessage bob hello there", 1) == CLI_SEND);
    CHECK(strcmp(R.line, "SEND|bob|hello there") == 0);

    CHECK(parse("sendmessage   bob    spaced   out   text  ", 1) == CLI_SEND);
    CHECK(strcmp(R.line, "SEND|bob|spaced   out   text") == 0);   /* inner spaces kept */

    CHECK(parse("SendMessage bob a|b|c", 1) == CLI_SEND);
    CHECK(strcmp(R.line, "SEND|bob|a|b|c") == 0);                 /* pipes allowed in text */

    CHECK(parse("sendmessage", 1) == CLI_ERROR && strstr(R.error, "Usage"));
    CHECK(parse("sendmessage bob", 1) == CLI_ERROR && strstr(R.error, "Usage"));
    CHECK(parse("sendmessage bob    ", 1) == CLI_ERROR);
    CHECK(parse("sendmessage b|ob hi", 1) == CLI_ERROR);

    char big[MAX_TEXT + 100], cmd[MAX_TEXT + 200];
    memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = '\0';
    snprintf(cmd, sizeof cmd, "sendmessage bob %s", big);
    CHECK(parse(cmd, 1) == CLI_ERROR && strstr(R.error, "too long"));

    char ok[MAX_TEXT + 1];
    memset(ok, 'y', MAX_TEXT); ok[MAX_TEXT] = '\0';
    snprintf(cmd, sizeof cmd, "sendmessage bob %s", ok);
    CHECK(parse(cmd, 1) == CLI_SEND);                              /* exactly the limit */
}

static void test_other_commands(void)
{
    CHECK(parse("getmessages bob", 1) == CLI_SEND && strcmp(R.line, "GETMSGS|bob") == 0);
    CHECK(parse("deletemessages carol", 1) == CLI_SEND && strcmp(R.line, "DELMSGS|carol") == 0);
    CHECK(parse("getuserlist", 1) == CLI_SEND && strcmp(R.line, "USERS") == 0);
    CHECK(parse("GETUSERLIST", 1) == CLI_SEND);
    CHECK(parse("getmessages", 1) == CLI_ERROR);
    CHECK(parse("getmessages a b", 1) == CLI_ERROR);               /* extra arg */
    CHECK(parse("deletemessages", 1) == CLI_ERROR);
    CHECK(parse("getmessages b|d", 1) == CLI_ERROR);
    CHECK(parse("shutdown", 1) == CLI_SHUTDOWN);
    CHECK(parse("frobnicate now", 1) == CLI_ERROR && strstr(R.error, "Unknown command"));
}

static void test_format(void)
{
    char out[MAX_LINE + 64];
    CHECK(cli_format("MSG|alice|hi there", out, sizeof out) == FMT_TEXT);
    CHECK(strcmp(out, "Message from alice: hi there") == 0);
    cli_format("MSG|alice|a|b|c", out, sizeof out);
    CHECK(strcmp(out, "Message from alice: a|b|c") == 0);

    setenv("TZ", "UTC", 1); tzset();
    cli_format("UNREAD|bob|1700000000|call me", out, sizeof out);
    CHECK(strcmp(out, "[unread 2023-11-14 22:13:20] Message from bob: call me") == 0);
    cli_format("HIST|alice|bob|1700000000|yo", out, sizeof out);
    CHECK(strcmp(out, "[2023-11-14 22:13:20] alice -> bob: yo") == 0);

    cli_format("USERLIST|alice,bob,carol", out, sizeof out);
    CHECK(strcmp(out, "Online users (3): alice, bob, carol") == 0);
    cli_format("USERLIST|alice", out, sizeof out);
    CHECK(strcmp(out, "Online users (1): alice") == 0);
    cli_format("USERLIST|", out, sizeof out);
    CHECK(strcmp(out, "No users online") == 0);

    cli_format("ERR|Invalid user 'zed'", out, sizeof out);
    CHECK(strcmp(out, "Error: Invalid user 'zed'") == 0);
    cli_format("OK|Delivered to bob", out, sizeof out);
    CHECK(strcmp(out, "Delivered to bob") == 0);
    CHECK(cli_format("SHUTDOWN|Server shutting down...", out, sizeof out) == FMT_SHUTDOWN);
    CHECK(strcmp(out, "Server shutting down...") == 0);
    cli_format("WEIRD|stuff", out, sizeof out);
    CHECK(strcmp(out, "WEIRD|stuff") == 0);
}

static void test_format_malformed_is_safe(void)
{
    char out[128];
    const char *bad[] = { "", "|", "MSG", "MSG|", "MSG|x", "UNREAD|x", "HIST|a|b", "USERLIST",
                          "ERR", "OK", "SHUTDOWN" };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        cli_format(bad[i], out, sizeof out);                  /* must not crash */
        CHECK(strlen(out) < sizeof out);
    }
    char longlist[MAX_LINE];                                  /* truncation in tiny buffer */
    strcpy(longlist, "USERLIST|");
    for (int i = 0; i < 100; i++) strcat(longlist, "user,");
    char small[40];
    cli_format(longlist, small, sizeof small);
    CHECK(strlen(small) < sizeof small);
}

static void test_name_ok(void)
{
    CHECK(cli_name_ok("alice") && cli_name_ok("A_b-c.9"));
    CHECK(!cli_name_ok("") && !cli_name_ok("a b") && !cli_name_ok("a|b") && !cli_name_ok("é"));
    char n33[34]; memset(n33, 'a', 33); n33[33] = '\0';
    CHECK(!cli_name_ok(n33));
    n33[32] = '\0';
    CHECK(cli_name_ok(n33));
}

int main(void)
{
    RUN(test_blank_and_help);
    RUN(test_connect);
    RUN(test_requires_connection);
    RUN(test_sendmessage);
    RUN(test_other_commands);
    RUN(test_format);
    RUN(test_format_malformed_is_safe);
    RUN(test_name_ok);
    TEST_DONE("test_cli");
}

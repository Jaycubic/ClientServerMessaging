#!/usr/bin/env bash
# setup_csit203_user.sh - create the shared, unprivileged team account and a
# ready-to-use build/test environment for this project.
#
# Run as root on the Linux server:
#
#     sudo CSIT203_PASSWORD='<password>' bash scripts/setup_csit203_user.sh
#
# (If CSIT203_PASSWORD is not set you are prompted for it. The password is
#  deliberately NOT stored in this repository.)
#
# What it does
#   1. makes sure the toolchain is installed (gcc, make, python3, git, nc)
#   2. creates the account (default CSIT203), sets its password
#   3. makes it a plain user: no sudo, no admin groups, private home (0700),
#      umask 077, process / login limits, no SSH port-forwarding or tunnelling
#   4. clones this repo into the user's home, builds it and runs the full test
#      suite *as that user* to prove the environment works
#   5. verifies the restrictions (cannot sudo, cannot apt install, cannot write
#      outside its home / /tmp) and prints a short report
#
# It is idempotent: running it again repairs/refreshes the account.
#
# Environment variables
#   CSIT_USER          account name              (default: CSIT203)
#   CSIT203_PASSWORD   account password          (prompted if unset)
#   REPO_URL           repository to clone       (default: the team repo)
#   HARDEN_SECRETS=1   also chmod o-rwx on world-readable .env / *.key / id_*
#                      files found under /opt, /srv, /var/www (see step 5)
set -euo pipefail

CSIT_USER="${CSIT_USER:-CSIT203}"
REPO_URL="${REPO_URL:-https://github.com/Jaycubic/ClientServerMessaging.git}"
PROJECT_DIR="ClientServerMessaging"

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33mWARNING: %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[[ $EUID -eq 0 ]] || die "run this script as root (sudo)"

# ---------------------------------------------------------------- 1. packages
say "1/5 Toolchain"
missing=()
for tool in gcc make python3 git nc; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if ((${#missing[@]})); then
    echo "installing missing tools: ${missing[*]}"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -y
    apt-get install -y build-essential python3 git netcat-openbsd
fi
for tool in gcc make python3 git nc; do
    printf '  %-8s %s\n' "$tool" "$(command -v "$tool")"
done

# ---------------------------------------------------------------- 2. account
say "2/5 Account '$CSIT_USER'"
if [[ -z "${CSIT203_PASSWORD:-}" ]]; then
    read -r -s -p "Password for $CSIT_USER: " CSIT203_PASSWORD; echo
    [[ -n "$CSIT203_PASSWORD" ]] || die "empty password"
fi

if id "$CSIT_USER" >/dev/null 2>&1; then
    echo "user exists - refreshing settings"
else
    if ! useradd --create-home --shell /bin/bash --user-group \
            --comment "CSIT203 team project (unprivileged)" "$CSIT_USER" 2>/dev/null; then
        # newer shadow-utils reject upper-case names unless --badname is given
        useradd --badname --create-home --shell /bin/bash --user-group \
            --comment "CSIT203 team project (unprivileged)" "$CSIT_USER" \
            || die "could not create user '$CSIT_USER'. Retry with CSIT_USER=csit203"
    fi
fi
printf '%s:%s\n' "$CSIT_USER" "$CSIT203_PASSWORD" | chpasswd
unset CSIT203_PASSWORD
usermod --shell /bin/bash "$CSIT_USER"
chage -M -1 "$CSIT_USER" 2>/dev/null || true       # password does not expire
HOME_DIR="$(getent passwd "$CSIT_USER" | cut -d: -f6)"

# ------------------------------------------------------------ 3. restrictions
say "3/5 Restrictions"

# 3a. no privileged group memberships
for g in sudo admin wheel adm lxd lxc docker disk shadow root staff video; do
    if getent group "$g" >/dev/null 2>&1 && id -nG "$CSIT_USER" | tr ' ' '\n' | grep -qx "$g"; then
        gpasswd -d "$CSIT_USER" "$g" >/dev/null
        echo "  removed from group $g"
    fi
done

# 3b. must have no sudo rights anywhere
if grep -rEq "^[^#]*\b${CSIT_USER}\b" /etc/sudoers /etc/sudoers.d 2>/dev/null; then
    die "an entry for $CSIT_USER exists in /etc/sudoers*; remove it, then re-run"
fi

# 3c. private home, restrictive default umask
chown -R "$CSIT_USER:$CSIT_USER" "$HOME_DIR"
chmod 700 "$HOME_DIR"
grep -q 'umask 077' "$HOME_DIR/.profile" 2>/dev/null || echo 'umask 077' >> "$HOME_DIR/.profile"
grep -q 'umask 077' "$HOME_DIR/.bashrc"  2>/dev/null || echo 'umask 077' >> "$HOME_DIR/.bashrc"

# 3d. resource limits (applied by PAM at login): stop fork bombs / runaway use
cat > /etc/security/limits.d/90-csit203.conf <<EOF
# written by setup_csit203_user.sh
$CSIT_USER  hard  nproc      512
$CSIT_USER  hard  nofile     4096
$CSIT_USER  hard  maxlogins  10
$CSIT_USER  hard  core       0
EOF
chmod 644 /etc/security/limits.d/90-csit203.conf

# 3e. SSH: shell access only - no port forwarding / tunnels / agent / X11
SSHD_DROPIN=/etc/ssh/sshd_config.d/60-csit203.conf
if [[ -d /etc/ssh/sshd_config.d ]] && grep -Eq '^\s*Include\s+/etc/ssh/sshd_config.d' /etc/ssh/sshd_config; then
    cat > "$SSHD_DROPIN" <<EOF
# written by setup_csit203_user.sh
Match User $CSIT_USER
    AllowTcpForwarding no
    AllowAgentForwarding no
    X11Forwarding no
    PermitTunnel no
    GatewayPorts no
EOF
    if sshd -t 2>/dev/null; then
        systemctl reload ssh 2>/dev/null || systemctl reload sshd 2>/dev/null || true
        echo "  ssh: forwarding/tunnelling disabled for $CSIT_USER (config validated, sshd reloaded)"
    else
        rm -f "$SSHD_DROPIN"
        warn "sshd -t rejected the drop-in; it was removed and sshd was NOT reloaded"
    fi
else
    warn "sshd_config does not Include sshd_config.d; add the Match User block manually"
fi
pw_auth="$(sshd -T -C "user=$CSIT_USER,host=localhost,addr=127.0.0.1" 2>/dev/null | awk '/^passwordauthentication/{print $2}')"
[[ "$pw_auth" == "yes" ]] || warn "SSH PasswordAuthentication is '${pw_auth:-unknown}' - password login may be refused; add the team's public keys to $HOME_DIR/.ssh/authorized_keys instead"

# ---------------------------------------------------------- 4. project + tests
say "4/5 Project environment (clone, build, test as $CSIT_USER)"
run_as() { sudo -H -u "$CSIT_USER" bash -lc "$1"; }
if [[ -d "$HOME_DIR/$PROJECT_DIR/.git" ]]; then
    run_as "cd ~/$PROJECT_DIR && git pull --ff-only"
else
    run_as "cd ~ && git clone '$REPO_URL' $PROJECT_DIR"
fi
run_as "cd ~/$PROJECT_DIR && make clean >/dev/null && make test 2>&1 | grep -E 'failures|^Ran|^OK|FAILED|rror'" \
    || die "the test suite failed for $CSIT_USER - see output above"

cat > "$HOME_DIR/README_FIRST.txt" <<EOF
CSIT203 shared account - Multi-User Client-Server Messaging
===========================================================
Project:   ~/$PROJECT_DIR           (git repo; build with 'make', test with 'make test')

Start the server (terminal 1):
    cd ~/$PROJECT_DIR && ./build/server -p 5555
Start clients (terminals 2..n):
    cd ~/$PROJECT_DIR && ./build/client
    > connect 127.0.0.1 alice          (valid users: see users.txt)
    > sendmessage bob hello
    > getmessages bob
    > getuserlist
    > shutdown
Use a different port if 5555 is taken:  ./build/server -p 6000  /  connect 127.0.0.1 alice 6000

Everything you create here is private to this account. You cannot use sudo or
install system packages; the compiler, make, python3, git and nc are already
installed. Please keep your experiments inside this home directory.
EOF
chown "$CSIT_USER:$CSIT_USER" "$HOME_DIR/README_FIRST.txt"
chmod 600 "$HOME_DIR/README_FIRST.txt"
grep -q README_FIRST "$HOME_DIR/.bash_profile" 2>/dev/null || \
    printf '[ -f ~/.profile ] && . ~/.profile\n[ -t 1 ] && cat ~/README_FIRST.txt\ncd ~/%s 2>/dev/null\n' "$PROJECT_DIR" \
        >> "$HOME_DIR/.bash_profile"
chown "$CSIT_USER:$CSIT_USER" "$HOME_DIR/.bash_profile"

# ----------------------------------------------------------- 5. verification
say "5/5 Verifying the restrictions"
fail=0
check() {   # check <description> <expected: deny|allow> <command...>
    local desc="$1" want="$2"; shift 2
    local rc=0
    sudo -H -u "$CSIT_USER" bash -c "$*" >/dev/null 2>&1 || rc=$?
    if { [[ $want == deny && $rc -ne 0 ]] || [[ $want == allow && $rc -eq 0 ]]; }; then
        printf '  [ok]   %s\n' "$desc"
    else
        printf '  [FAIL] %s\n' "$desc"; fail=1
    fi
}
check "cannot use sudo"                         deny  "sudo -n true"
check "is not in sudo/admin/adm/lxd/docker"     deny  "id -nG | tr ' ' '\n' | grep -Eqx 'sudo|admin|adm|lxd|docker|root'"
check "cannot apt-get install"                  deny  "apt-get install -y --no-install-recommends cowsay"
check "cannot write /etc"                       deny  "touch /etc/csit203_probe"
check "cannot write /usr/local"                 deny  "touch /usr/local/csit203_probe"
check "cannot write /opt"                       deny  "touch /opt/csit203_probe"
check "cannot read /etc/shadow"                 deny  "cat /etc/shadow"
check "cannot enter other users' homes"         deny  "for d in /home/*; do [ \"\$d\" = \"$HOME_DIR\" ] && continue; ls \"\$d\" && exit 0; done; exit 1"
check "can create files and folders in home"    allow "mkdir -p ~/probe/a/b && echo x > ~/probe/a/b/f && rm -rf ~/probe"
check "can compile C with pthreads"             allow "cd \$(mktemp -d) && printf '#include <pthread.h>\nint main(){return 0;}' > t.c && gcc -pthread t.c -o t && ./t"
check "home is private (mode 700)"              allow "[ \"\$(stat -c %a ~)\" = 700 ]"

# Who else could read secrets on this machine? (a normal user can read anything
# that is world-readable, so a 'limited' account is only as private as the files.)
echo
echo "World-readable secrets visible to $CSIT_USER (should be empty):"
mapfile -t exposed < <(find /opt /srv /var/www -xdev -type f \
        \( -name '.env' -o -name '.env.*' -o -name '*.key' -o -name '*.pem' -o -name 'id_rsa' -o -name 'id_ed25519' \) \
        ! -name '*.example' ! -name '*.pub' -perm -o+r 2>/dev/null | head -50)
if ((${#exposed[@]})); then
    printf '  %s\n' "${exposed[@]}"
    if [[ "${HARDEN_SECRETS:-0}" == 1 ]]; then
        for f in "${exposed[@]}"; do chmod o-rwx "$f"; done
        echo "  -> HARDEN_SECRETS=1: removed 'other' access from the files above"
        echo "     (if a service runs as another user and now fails, give that file to the service's group)"
    else
        warn "the files above are readable by every local user, including $CSIT_USER."
        echo "     Re-run with HARDEN_SECRETS=1 to chmod o-rwx them, or fix them yourself."
        fail=1
    fi
else
    echo "  none found"
fi

echo
if ((fail)); then
    warn "setup finished, but some checks FAILED - review the output above"
    exit 1
fi
say "Done. Share:  ssh $CSIT_USER@<server-address>   (project is in ~/$PROJECT_DIR)"

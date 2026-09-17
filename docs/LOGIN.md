# Command-line accounts and `starty`

OpenRFS boots to the command line. The desktop is not constructed during a
normal boot.

## Create the first user

Run `useradd NAME`. Names may contain letters, numbers, `-`, and `_`, must begin
with a letter or number, and are limited to 31 characters. The shell then asks
for an 8-64 character password twice without echoing it.

OpenRFS stores one credential record at `OPENRFS/LOGIN.DAT` on the writable data
volume. The record contains the username, a 128-bit per-install salt from the OpenRFS random source, an
iterated SHA-256 password digest, its work factor, and a checksum. The write is
staged through `LOGIN.NEW`, synchronized, renamed, and synchronized again.
Account creation is refused when the initialized random source or durable writable
storage is unavailable.

## Start the desktop

Run `starty`. The command asks for the username and password, verifies them,
and only then constructs and activates the graphical desktop. A failed login
returns to the command prompt without starting the UI.

## Current limits

This is a single-account bootstrap model. There is no root/user separation,
password change command, recovery mechanism, lockout timer, per-file identity
enforcement, encrypted home directory, or multi-user permission model yet. The
iterated SHA-256 record is a bounded early implementation, not a memory-hard
password KDF. Those limits must be resolved before account authentication can
support a strong privacy claim.

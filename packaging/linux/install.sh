#!/usr/bin/env sh
set -eu

APP_ID="cossacks-mod-launcher"
APP_NAME="Cossacks Mod Launcher"
APPIMAGE_INSTALL_NAME="CossacksModLauncher.AppImage"
ICON_NAME="cossacks-mod-launcher"
ICON_FILE_NAME="$ICON_NAME.png"
VERSION_FILE_NAME="VERSION"

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)

if [ -z "${XDG_DATA_HOME:-}" ]; then
    if [ -z "${HOME:-}" ]; then
        echo "HOME is not set; cannot choose a user-local install directory." >&2
        exit 1
    fi
    XDG_DATA_HOME="$HOME/.local/share"
fi

HOME_DIR=${HOME:-}
INSTALL_DIR=${CML_INSTALL_DIR:-"$XDG_DATA_HOME/$APP_ID"}
APPLICATIONS_DIR="$XDG_DATA_HOME/applications"
ICON_THEME_DIR="$XDG_DATA_HOME/icons/hicolor"
ICON_DIR="$ICON_THEME_DIR/256x256/apps"
ICON_FILE="$ICON_DIR/$ICON_FILE_NAME"
DESKTOP_FILE="$APPLICATIONS_DIR/$APP_ID.desktop"
APPIMAGE_TARGET="$INSTALL_DIR/$APPIMAGE_INSTALL_NAME"
INSTALLER_TARGET="$INSTALL_DIR/install.sh"
ICON_TARGET="$INSTALL_DIR/$ICON_FILE_NAME"
VERSION_TARGET="$INSTALL_DIR/$VERSION_FILE_NAME"
PACKAGE_ICON="$SCRIPT_DIR/$ICON_FILE_NAME"
PACKAGE_VERSION_FILE="$SCRIPT_DIR/$VERSION_FILE_NAME"
ACTION=install
ASSUME_YES=no

read_version_file() {
    version_file=$1

    if [ -f "$version_file" ]; then
        version=
        IFS= read -r version < "$version_file" || version=
        if [ -n "$version" ]; then
            printf '%s\n' "$version"
            return 0
        fi
    fi

    printf 'unknown\n'
}

package_version() {
    read_version_file "$PACKAGE_VERSION_FILE"
}

installed_version() {
    if [ -f "$APPIMAGE_TARGET" ]; then
        read_version_file "$VERSION_TARGET"
    else
        printf 'not installed\n'
    fi
}

show_help() {
    cat <<EOF
Usage:
  ./install.sh                     Install or update $APP_NAME for the current user
  ./install.sh --uninstall         Remove the current user's $APP_NAME installation
  ./install.sh --uninstall --yes   Remove without asking for confirmation
  ./install.sh --help              Show this help

Versions:
  Package:   $(package_version)
  Installed: $(installed_version)

Installs to:
  AppImage:      $APPIMAGE_TARGET
  Desktop entry: $DESKTOP_FILE
  Icon:          $ICON_FILE

Set CML_INSTALL_DIR to override the AppImage install directory.
EOF
}

find_appimage() {
    for candidate in "$SCRIPT_DIR"/CossacksModLauncher*.AppImage "$SCRIPT_DIR"/*.AppImage; do
        [ -f "$candidate" ] || continue
        case "${candidate##*/}" in
            linuxdeploy*.AppImage) continue ;;
        esac

        printf '%s\n' "$candidate"
        return 0
    done

    return 1
}

canonical_file_path() {
    file_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd -P)
    file_name=${1##*/}
    printf '%s/%s\n' "$file_dir" "$file_name"
}

copy_file_if_different() {
    source_file=$1
    target_file=$2
    file_mode=$3

    if [ ! -f "$source_file" ]; then
        return 1
    fi

    source_path=$(canonical_file_path "$source_file")
    if [ -f "$target_file" ]; then
        target_path=$(canonical_file_path "$target_file")
        if [ "$source_path" = "$target_path" ]; then
            chmod "$file_mode" "$target_file" 2>/dev/null || true
            return 0
        fi
    fi

    cp "$source_file" "$target_file"
    chmod "$file_mode" "$target_file"
}

version_is_newer() {
    left_version=$1
    right_version=$2

    awk -v left_version="$left_version" -v right_version="$right_version" '
        function parse_version(version, parts, raw_parts, part_count, part_index) {
            if (version !~ /^[0-9]+([.][0-9]+)*$/) {
                return 1
            }

            part_count = split(version, raw_parts, ".")
            for (part_index = 1; part_index <= 3; part_index++) {
                parts[part_index] = part_index <= part_count ? raw_parts[part_index] + 0 : 0
            }

            return 0
        }

        BEGIN {
            if (parse_version(left_version, left_parts) || parse_version(right_version, right_parts)) {
                exit 1
            }

            for (part_index = 1; part_index <= 3; part_index++) {
                if (left_parts[part_index] > right_parts[part_index]) {
                    exit 0
                }
                if (left_parts[part_index] < right_parts[part_index]) {
                    exit 1
                }
            }

            exit 1
        }
    '
}

confirm_install() {
    already_installed=$1
    new_version=$(package_version)
    current_version=$(installed_version)
    default_answer=yes

    if [ "$already_installed" = "yes" ]; then
        echo "$APP_NAME is already in the install directory."
        echo "This will refresh the desktop entry, icon, and version metadata."
    else
        echo "This will install or update $APP_NAME for the current user."
    fi

    echo "Package version: $new_version"
    echo "Installed version: $current_version"
    if version_is_newer "$current_version" "$new_version"; then
        echo "A newer version is already installed; keeping it is recommended."
        default_answer=no
    fi
    echo "AppImage: $APPIMAGE_TARGET"
    echo "Desktop entry: $DESKTOP_FILE"
    echo "Icon: $ICON_FILE"

    if [ "$ASSUME_YES" = "yes" ]; then
        echo "Continuing because --yes was passed."
        return 0
    fi

    if [ "$default_answer" = "yes" ]; then
        printf 'Continue? [Y/n] '
    else
        printf 'Continue? [y/N] '
    fi

    answer=
    if ! IFS= read -r answer; then
        answer=
    fi

    if [ -z "$answer" ]; then
        answer=$default_answer
    fi

    case "$answer" in
        y|Y|yes|YES|Yes) return 0 ;;
        n|N|no|NO|No) ;;
        *) ;;
    esac

    echo "Installation cancelled."
    return 1
}

desktop_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/`/\\`/g; s/\$/\\$/g'
}

refresh_desktop_database() {
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
    fi

    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q "$ICON_THEME_DIR" >/dev/null 2>&1 || true
    fi
}

install_icon() {
    if [ ! -f "$PACKAGE_ICON" ]; then
        echo "Warning: bundled icon not found: $PACKAGE_ICON" >&2
        return 1
    fi

    copy_file_if_different "$PACKAGE_ICON" "$ICON_TARGET" 644
    copy_file_if_different "$PACKAGE_ICON" "$ICON_FILE" 644
}

install_version_file() {
    if [ -f "$PACKAGE_VERSION_FILE" ]; then
        copy_file_if_different "$PACKAGE_VERSION_FILE" "$VERSION_TARGET" 644
    else
        echo "Warning: bundled version file not found: $PACKAGE_VERSION_FILE" >&2
    fi
}

write_desktop_file() {
    exec_path=$(desktop_escape "$APPIMAGE_TARGET")
    uninstall_path=$(desktop_escape "$INSTALLER_TARGET")

    cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
GenericName=$APP_NAME
Comment=Manage Cossacks 3 mods
Exec="$exec_path"
Icon=$ICON_NAME
Terminal=false
Categories=Game;Utility;
StartupWMClass=CossacksModLauncher
Actions=Uninstall;

[Desktop Action Uninstall]
Name=Uninstall $APP_NAME
Exec="$uninstall_path" --uninstall --yes
Icon=$ICON_NAME
EOF

    chmod 644 "$DESKTOP_FILE"
}

install_app() {
    if ! appimage_source=$(find_appimage); then
        echo "Could not find a Cossacks Mod Launcher AppImage next to install.sh." >&2
        exit 1
    fi

    source_path=$(canonical_file_path "$appimage_source")
    already_installed=no

    if [ -f "$APPIMAGE_TARGET" ]; then
        target_path=$(canonical_file_path "$APPIMAGE_TARGET")
        if [ "$source_path" = "$target_path" ]; then
            already_installed=yes
        fi
    fi

    confirm_install "$already_installed" || exit 0

    mkdir -p "$INSTALL_DIR" "$APPLICATIONS_DIR" "$ICON_DIR"

    if [ "$already_installed" = "yes" ]; then
        echo "AppImage is already installed; skipping copy."
    else
        cp "$appimage_source" "$APPIMAGE_TARGET"
        chmod +x "$APPIMAGE_TARGET"
    fi

    script_source="$SCRIPT_DIR/${0##*/}"
    if [ -f "$script_source" ]; then
        copy_file_if_different "$script_source" "$INSTALLER_TARGET" 755
    fi

    install_icon || true
    install_version_file
    write_desktop_file
    refresh_desktop_database

    echo "$APP_NAME installed."
    echo "Version: $(installed_version)"
    echo "AppImage: $APPIMAGE_TARGET"
    echo "Desktop entry: $DESKTOP_FILE"
}

confirm_uninstall() {
    echo "This will uninstall $APP_NAME for the current user."
    echo "Installed version: $(installed_version)"
    echo "The following known files will be removed if they exist:"
    echo "AppImage: $APPIMAGE_TARGET"
    echo "Installer: $INSTALLER_TARGET"
    echo "Version: $VERSION_TARGET"
    echo "Bundled icon: $ICON_TARGET"
    echo "Desktop icon: $ICON_FILE"
    echo "Desktop entry: $DESKTOP_FILE"
    echo "The install directory itself will be kept."

    if [ "$ASSUME_YES" = "yes" ]; then
        echo "Continuing because --yes was passed."
        return 0
    fi

    printf 'Continue? [y/N] '
    answer=
    if ! IFS= read -r answer; then
        answer=
    fi

    case "$answer" in
        y|Y|yes|YES|Yes) return 0 ;;
        *)
            echo "Uninstall cancelled."
            return 1
            ;;
    esac
}

uninstall_app() {
    confirm_uninstall || exit 0

    case "$INSTALL_DIR" in
        ""|"/"|"$HOME_DIR"|"$XDG_DATA_HOME")
            echo "Refusing to remove unsafe install directory: $INSTALL_DIR" >&2
            exit 1
            ;;
    esac

    rm -f \
        "$APPIMAGE_TARGET" \
        "$INSTALLER_TARGET" \
        "$VERSION_TARGET" \
        "$ICON_TARGET" \
        "$DESKTOP_FILE" \
        "$ICON_FILE"

    refresh_desktop_database
    echo "$APP_NAME uninstalled."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --uninstall)
            ACTION=uninstall
            ;;
        --yes|-y)
            ASSUME_YES=yes
            ;;
        -h|--help)
            ACTION=help
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo >&2
            show_help >&2
            exit 1
            ;;
    esac
    shift
done

case "$ACTION" in
    install)
        install_app
        ;;
    uninstall)
        uninstall_app
        ;;
    help)
        show_help
        ;;
    *)
        echo "Unknown action: $ACTION" >&2
        echo >&2
        show_help >&2
        exit 1
        ;;
esac

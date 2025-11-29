# Maintainer: Your Name <you@example.com>
pkgname=crash-reporter
pkgver=0.1.0
pkgrel=1
pkgdesc="Collect system diagnostics, generate AI summary, and file a GitHub issue"
arch=('x86_64')
url="https://github.com/AcreetionOS-Linux/crash-reporter"
license=('MIT')
depends=('gtk3' 'curl' 'jansson' 'polkit')
makedepends=('gcc' 'pkg-config' 'gtk3' 'libcurl' 'jansson')
source=()
sha256sums=()

build() {
  cd "$srcdir"
  echo "Building crash_reporter..."
  gcc -o crash_reporter src/crash_reporter.c src/crash_reporter_gui.c $(pkg-config --cflags --libs gtk+-3.0) -lcurl -ljansson
}

package() {
  cd "$srcdir"
  install -d "$pkgdir/usr/bin"
  install -m 755 crash_reporter "$pkgdir/usr/bin/crash_reporter"

  install -d "$pkgdir/usr/share/applications"
  if [ -f "crash-reporter.desktop" ]; then
    install -m 644 crash-reporter.desktop "$pkgdir/usr/share/applications/crash-reporter.desktop"
  fi

  install -d "$pkgdir/usr/share/doc/$pkgname"
  if [ -f "README.md" ]; then
    install -m 644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
  fi
  if [ -f "LICENSE" ]; then
    install -m 644 LICENSE "$pkgdir/usr/share/doc/$pkgname/LICENSE"
  fi
}

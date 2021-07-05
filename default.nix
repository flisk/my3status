with import <nixpkgs> {};

stdenv.mkDerivation rec {
  pname = "my3status";
  version = "git";

  src = ./.;

  nativeBuildInputs = [ pkgconfig ];
  buildInputs = [ pulseaudio glib.dev sqlite ];

  makeFlags = [ "DESTDIR=$(out) PREFIX= CFLAGS=-DUPOWER=1" ];
}

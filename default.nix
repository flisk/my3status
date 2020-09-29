with import <nixpkgs> {};

stdenv.mkDerivation rec {
  pname = "my3status";
  version = "git";

  src = ./.;

  nativeBuildInputs = [ pkgconfig ];
  buildInputs = [ pulseaudio glib ];

  makeFlags = [ "DESTDIR=$(out) PREFIX=" ];
}

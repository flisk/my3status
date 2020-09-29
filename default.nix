with import <nixpkgs> {};

stdenv.mkDerivation rec {
  name = "my3status-${version}";
  version = "git";
  src = ./.;

  nativeBuildInputs = [ pkgconfig ];
  buildInputs = [ pulseaudio glib ];

  installPhase = ''
	  install -D ./my3status $out/bin/my3status
  '';
}

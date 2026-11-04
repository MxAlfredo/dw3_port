Mod del port de Digimon World 3
================================

Esta carpeta es un mod. Instalarlo es copiarla en <preferencias>/mods/; quitarlo es borrarla.
El puerto nunca escribe aqui. Un archivo que el puerto rechaza queda en el log (carpeta logs/) con el motivo.

mod.json            obligatorio. "format": "mod-v1"; "kind": "assets" (reemplaza recursos) o "content"
                    (agrega objetos, tecnicas, Digimon o cartas). "version" son digitos y puntos (1.2.0).
                    "port": ">=0.4" pide una version minima del puerto; "requires": ["otro-mod"] lo
                    hace depender de otros mods (por nombre de carpeta).
tables/<tabla>.json  parches de tablas del juego (recompensas, botin, precios, tiendas...).
content/<tipo>.json  contenido nuevo: items.json, techs.json, digimon.json, cards.json (solo con "kind": "content").
card/<llave>.png    arte de las cartas nuevas de content/cards.json (PNG indexado 32x32).
config/              tiempos y banner (stage_banner.json, transitions.json); solo las claves que cambias.
text/<idioma>/       textos del juego, un JSON por archivo de texto.
images/              imagenes PNG indexadas, con el mismo tamano que la original.
audio/<BANCO>/       efectos (vag_NNN.wav) y musica (seq_P_RR.wav) en WAV de 16 bits.
video/<NOMBRE>.avi   peliculas AVI con MJPEG.
files/               archivos del disco, sin cambios de formato, por su ruta sin el AAA/.

Probar el mod sin abrir el juego:
  boot "<disco>.bin" --validate-mod <nombre>     (un problema por linea; codigo de salida distinto de 0 si hay errores)
  boot --list-mods                               (que mods hay, si estan activos y en que orden)

Orden de carga: en settings.ini, [mods] order = a, b (el ultimo gana). Sin esa clave, todos en orden alfabetico.
La guia completa esta en docs/modding/README.md (README.en.md en ingles). No incluyas datos del juego
(textos, imagenes, audio) en un mod que compartas.

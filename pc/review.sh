#!/bin/sh
# Owner review shortcuts: each scenario boots straight into one scene, with
# sound, and stays playable from the keyboard (the harness only overrides the
# pad while a scripted key is held). Prints what to look and listen for.
#
#   pc/review.sh <scenario> [disc.bin]
#
# Scenarios: batalla, resultados, cambio, libre, inicio, digilab, pelicula, video2, menu, cargar, laboratorio, nombre, gimnasio, tienda, tiendacartas, mazo, album, cartas, controles (alias: opciones), betamon, seadramon, betamonpesca.
# Every scenario ends by itself when its scene is done, read from the game's
# mode (harness tokens `until:<hex>`, implied by `mode:` and the battle tokens;
# the log prints "end: left mode 0x.. for 0x.. -> end of run"). Only `libre`
# stays open on purpose (token `stay`). Closing the window always ends a run.
set -e
cd "$(dirname "$0")/.."
disc="${2:-build/t9-e2e/DW3_EU_ES419_TEXT_TEST.bin}"
shot=pc/build/review.png

case "$1" in
batalla)
    keys="battle"
    echo "Batalla directa (Kotemon vs enemigo de nivel 12)."
    echo "Revisar: cámara y luz, nombres/HP/menú, cursor al moverte,"
    echo "música y efectos de los ataques (capas, que nada se corte)."
    echo "Termina solo al salir de la batalla."
    ;;
resultados)
    # The battle tokens' implied end covers the results scene (its own mode,
    # 0x1400): the run ends when the results are left.
    keys="battleauto"
    echo "Batalla ganada sola por el piloto automático (menú, Tech, mensajes)"
    echo "hasta la pantalla de resultados (EXP, dinero, objeto) y la vuelta al campo."
    echo "Termina solo al salir de la pantalla de resultados."
    echo "Revisar: textos de la pantalla de resultados, números de EXP y BIT,"
    echo "paneles de los tres compañeros, fundido final. El log trae 'auto N: ...'."
    ;;
cambio)
    keys="battle@150:down,10,down,10,down,10,cross"
    echo "Batalla con el menú de cambio (Tag) ya abierto: elegí el digimon."
    echo "Revisar: que el sonido del cambio termine solo, sin quedar en loop."
    echo "Termina solo al salir de la batalla."
    ;;
libre)
    # Progress 4 is the first one where START opens the field menu
    # (src/field/field_mode/field.c openFieldMenu: GAME.progress < 4 blocks it);
    # the warp's default progress is 0, so the menu stayed shut.
    keys="stay,warp:330,4"
    echo "Juego libre: Parque Central (escenario 330, progreso 4), abierto para caminar."
    echo "Start abre el menú del campo (Items, Sort, Map, Techniques, Status)."
    echo "Revisar: fluidez al caminar (60 FPS; Select o F1 > Display > Show FPS), escenario y música."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
inicio)
    # The game as the player starts it: title, NEW GAME and the story from its
    # first scene, with no harness shortcut. `stay` keeps the run open.
    keys="stay"
    # one log line per battle turn and per paralysis roll (pc/shim/battle_trace.h)
    export DW3_BATTLE_EVENT_LOG=1
    echo "Juego desde el principio: título, NEW GAME e historia normal."
    echo "Revisar: lo que pida la tarea (por ejemplo Auto text en los primeros diálogos)."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
digilab)
    keys="warp:200,3,flag:400D,fieldauto,until:D00"
    echo "Calle de Asuka tras la primera batalla (progreso 3): el señor de la salida"
    echo "habla de nuevo (el piloto de diálogos aprieta cruz solo) y el relato te lleva"
    echo "al Digilab. Revisar: que la transición termine en el Digilab y que puedas"
    echo "caminar ahí (antes quedabas congelado). El piloto se apaga al llegar al"
    echo "Digilab ('fieldauto N: off' en el log): desde ahí jugás vos."
    echo "Termina solo cuando abrís el laboratorio del doctor (0xD00) y lo cerrás."
    ;;
menu)
    keys="mode:1000"
    echo "Menú del campo (pantalla de objetos de STSTATUS, con el grupo inicial)."
    echo "Revisar: textos y paneles, cursor, cruz para entrar, triángulo para"
    echo "volver; \"mode:1000,4\" abre directo la pantalla de estado (0-5)."
    echo "Termina solo al cerrar el menú (triángulo hasta salir)."
    ;;
cargar)
    keys="mode:C00,0"
    echo "Pantalla de cargar (STGMCARD). Las partidas son archivos locales .dw3save:"
    echo "\$DW3_SAVES_DIR, o saves/default de la carpeta del usuario (~/.local/share/dw3port/dw3/; \$DW3_USER_DIR la cambia)."
    echo "Si no hay partidas y hay un .mcd en import/ (o \$DW3_MEMCARD) se importa una vez."
    echo "Con el slot 1: sin partidas = crear el archivo (solo al guardar); con partida = la lista."
    echo "Para guardar: pc/build/boot con \"mode:C00,save\"."
    echo "Termina solo al cerrar la pantalla (al volver al campo o al título)."
    ;;
laboratorio)
    keys="mode:D00"
    echo "Laboratorio Digimon (STGDGLAB) con el grupo inicial, como al hablar con el"
    echo "doctor en el campo (vuelve al campo 0x206 al salir). Revisar: las tres"
    echo "opciones (cambiar, digievolucionar, tabla), cruz para entrar, triángulo"
    echo "para volver. Sin entradas aprendidas, 'Digivolve' dice que no se puede"
    echo "cargar una técnica (el estado de partida nueva)."
    echo "Termina solo al salir del laboratorio (triángulo en el menú)."
    ;;
nombre)
    keys="mode:B00"
    echo "Poner nombre al compañero (STDGNAME) con el grupo inicial. Revisar: elegir"
    echo "digimon con cruz, teclado, cruz escribe, start salta a OK, cruz confirma;"
    echo "el nombre nuevo sale en la lista; triángulo vuelve al campo."
    echo "Termina solo al salir de la pantalla."
    ;;
gimnasio)
    keys="mode:A00,0"
    echo "Gimnasio (STGTRAIN, gimnasio 0 = Asuka centro; \"mode:A00,<0-7>\" cambia)"
    echo "con 30 TP por compañero. Revisar: cruz elige digimon, entrenamiento y TP,"
    echo "\"Is this ok?\" y las tres tandas (cruz en cada intento), el mensaje de"
    echo "resultados (+fuerza), triángulo para salir al campo."
    echo "Termina solo al salir del gimnasio."
    ;;
tienda)
    keys="mode:F00,0"
    echo "Tienda de objetos (STITSHOP, tienda 0 = Asuka Armory) con 5000 BIT y un"
    echo "objeto de cada uno de los primeros 63 (para vender). Revisar: BUY/SELL,"
    echo "listas por categoría, precios, \"How many?\", confirmación Buy/Sell y que el"
    echo "dinero cambie (comprar resta, vender suma); triángulo para volver."
    echo "Termina solo al salir de la tienda (triángulo en el menú)."
    ;;
tiendacartas)
    keys="mode:1300,49"
    echo "Tienda de cartas (STCRDSHP, tienda 49 = AS Yellow Cruiser) con 50000 BIT."
    echo "Revisar: Buy One Card / Open Booster / Edit Folder; en la compra, la fila"
    echo "de cartas, precio (10000 BIT), cantidad en mazo y que el dinero baje."
    echo "Open Booster dice 'No boosters' (sin sobres en el estado de partida nueva)."
    echo "Termina solo al salir de la tienda (triángulo en el menú). Edit Folder abre el editor"
    echo "de mazo (otro modo, 0x400) y por eso termina la corrida: el editor se prueba con mazo."
    ;;
mazo)
    keys="mode:400,settle,battle@0:cross,cross,cross,cross,cross,triangle,triangle,triangle"
    echo "Editor de mazo (STCRDDEK) con el mazo inicial y 48 cartas más en la bolsa."
    echo "El piloto elige Folder 1, Edit Folder, reemplaza la primera carta del mazo"
    echo "por la primera de la lista y vuelve a salir. Revisar: la cuadrícula de 40"
    echo "cartas, el cambio de la primera carta (los cubos de color cambian) y la lista."
    echo "Termina solo al salir del editor (0x400)."
    ;;
album)
    keys="mode:1200,settle,battle@0:right,right,r1,triangle"
    echo "Álbum de cartas (STCRDABM) con 48 cartas vistas. El piloto mueve el cursor,"
    echo "pasa a la página 2 con R1 y sale. Revisar: página 1/27, nombre y texto de la"
    echo "carta bajo el cursor, el giro de página y que no falte ninguna imagen."
    echo "Termina solo al salir del álbum (0x1200)."
    ;;
cartas)
    keys="cardauto"
    echo "Combate de cartas (CARDGAME) contra el oponente 1 de Asuka City (modo 0x700, arg 3), con el"
    echo "mazo inicial. El piloto lee el estado del juego y aprieta lo que espera:"
    echo "elige carpeta, el sorteo de quién empieza, baja las cartas que puede pagar,"
    echo "responde Sí/No y los mensajes, hasta el fin del duelo (el log trae 'card N:' y"
    echo "'auto N:'). Revisar: mano de seis cartas con nombre, AP/HP de cada lado,"
    echo "cubos de puntos, el giro de las cartas del rival, las ventanas de ronda"
    echo "y el mensaje final. Dura unos 70 s. Termina solo al salir del combate (0x700)."
    ;;
controles|opciones)
    keys="stay,warp:330"
    echo "Menú de opciones (F1 o Select) sobre el juego libre (Parque Central)."
    echo "Pestañas Controls/Video/Display/Language/Audio con Q/E, RePág/AvPág o L1/R1 (las vacías"
    echo "dicen Coming soon). Revisar: F1 o Select abre y cierra; flechas o cruceta mueven, Enter o A empieza a"
    echo "asignar (la fila dice 'Press a key...'), Escape cancela; izquierda/derecha"
    echo "cambian Keyboard/Controller; Reset to defaults vuelve a los valores de fábrica."
    echo "Backspace y F1-F12 no se pueden asignar. Mientras está abierta el juego no"
    echo "recibe botones (sigue corriendo debajo). Se guarda en settings.ini (o \$DW3_SETTINGS)."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
video2)
    keys="mode:E03,movieskip:75"
    echo "Video ACCESS DENIED (el de antes de ir al Digimundo)."
    echo "Revisar: que ocupe la pantalla con bandas negras, sin fondo azul."
    echo "Reproduce unos 5 segundos y lo salta solo (Start); termina al salir del modo 0xE03."
    ;;
pelicula)
    # The opening movie is the STDWTITL mode 0xE02 here (country_select.c:652
    # asks MODE_OPENING 0xE01 for LANGUAGE 0, MODE_OPENING_2 0xE02 otherwise;
    # this disc boots 0xE02: log "boot: mode 0xE02"). When the player finishes,
    # movie.c:264 requests the movie's next mode, 0xE00 (the title), so the
    # movie is done on the first frame that leaves 0xE02. A prefix match on
    # 0xE00 would not do: the title shares that overlay. The run plays 75 movie
    # frames (15 fps, about 5 s; the St layer's delivered-frame counter) and
    # skips with Start, which the player checks at src/menus/title/movie.c:173.
    keys="start,until:E02,movieskip:75"
    echo "Película de apertura (confirma el idioma y arranca)."
    echo "Revisar: velocidad e imagen sincronizadas con el audio."
    echo "Reproduce unos 5 segundos y la salta sola (Start); termina al volver el título."
    ;;
betamon)
    pack=1
    keys="stay,warp:330,4"
    export DW3_STARTER_SET=betamon-pack:river
    export DW3_SEED_PARTNER="8.0=betamon-pack:betamon@5/0"
    export DW3_SEED_PARTNER_LEVEL="8=9"
    echo "Paquete Betamon, primera evolución: Parque Central (escenario 330) con el equipo Río"
    echo "(Veemon, Betamon, Kumamon; en esta copia Betamon va al frente porque la EXP la recibe el"
    echo "que pelea). Betamon está en nivel 9, con la forma al nivel 5: a UNA batalla ganada del"
    echo "nivel 10, que son los umbrales reales del paquete."
    echo "Hacer: caminá en el pasto hasta una batalla (o usá un enemigo cualquiera) y ganala."
    echo "Revisar: en resultados sale el nivel 10 y «aprendió Seadramon». En la siguiente batalla,"
    echo "menú DV (digievolución): Betamon -> Seadramon con su modelo; habilidades y daño razonables."
    echo "Las partidas de esta prueba van a pc/build/review-saves; tus mods y partidas no se tocan."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
seadramon)
    pack=1
    keys="stay,warp:330,4"
    export DW3_STARTER_SET=betamon-pack:river
    export DW3_SEED_PARTNER="8.0=betamon-pack:betamon@5/1,8.1=betamon-pack:seadramon@20/0"
    export DW3_SEED_PARTNER_LEVEL="8=29"
    echo "Paquete Betamon, segunda evolución: Parque Central (escenario 330), equipo Río con Betamon"
    echo "al frente. Seadramon ya está aprendido y es la forma con que empieza la batalla (forma al"
    echo "nivel 20, nivel de Betamon 29): a UNA batalla ganada del nivel 30, umbrales reales del paquete."
    echo "Hacer: ganá una batalla. Revisar: nivel 30 y «aprendió MegaSeadramon» en resultados."
    echo "En la siguiente batalla, menú DV: Seadramon -> MegaSeadramon (modelo y encuadre)."
    echo "Las partidas de esta prueba van a pc/build/review-saves; tus mods y partidas no se tocan."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
betamonpesca)
    pack=1
    # Progress 20 is inside the story window of the quest (special 0x24, progress 14-38); the three default partners (0, 6, 7) are
    # put at level 24 (the harness seed, applied when the stage is made), so their levels add up to 72: inside 45-74, the window
    # the fisher asks for (the same two thresholds as the stage's own Patamon and Renamon givers).
    keys="stay,warp:330,20"
    export DW3_SEED_PARTNER_LEVEL="0=24,6=24,7=24"
    echo "Paquete Betamon, misión del DDNA (como las misiones de DDNA del juego): Parque Central (escenario 330,"
    echo "progreso 20) con el equipo por defecto en nivel 24 (suman 72: la misión pide entre 45 y 74)."
    echo "Hacer: 1) hablale al pescador (abajo, cerca de x 900, y 960): se presenta; 2) hablale otra vez: te cuenta de"
    echo "MegaSeadramon y la pantalla se vuelve a armar (la serpiente aparece en la orilla, a la derecha, cerca de x 1010, y 1012);"
    echo "3) hablale a la serpiente: batalla de evento contra MegaSeadramon (no se puede huir); 4) al ganar, hablale otra vez:"
    echo "te deja el DDNA de Betamon (objeto «Betamon DDNA») con el ícono de «objeto recibido» sobre tu cabeza;"
    echo "5) llevale el DDNA al pescador: desbloquea a Betamon (ícono de nuevo; el DDNA se queda en tu bolsa, como en el original)."
    echo "Revisar: textos en es-419, que la serpiente solo aparece tras el paso 2 y desaparece al tener el DDNA, y en el"
    echo "laboratorio o la memory card que Betamon ya aparece desbloqueado; si hablás de nuevo con el pescador no repite."
    echo "Las partidas de esta prueba van a pc/build/review-saves; tus mods y partidas no se tocan."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
*)
    echo "uso: pc/review.sh batalla|resultados|cambio|libre|inicio|digilab|pelicula|video2|menu|cargar|laboratorio|nombre|gimnasio|tienda|tiendacartas|mazo|album|cartas|controles|opciones|betamon|seadramon|betamonpesca [disco.bin]" >&2
    exit 2
    ;;
esac

make -C pc >/dev/null
if [ -n "$pack" ]; then
    # The Betamon pack in a fresh copy and fresh saves, so the owner's mods and saves stay untouched.
    rm -rf pc/build/review-mods pc/build/review-saves
    mkdir -p pc/build/review-mods pc/build/review-saves
    cp -r pc/examples/mods/betamon-pack pc/build/review-mods/
    # Betamon takes the exp only when it fights, so the copy puts it first in the River team
    sed -i 's|"dw3:partner/4", "betamon-pack:betamon"|"betamon-pack:betamon", "dw3:partner/4"|' pc/build/review-mods/betamon-pack/content/teams.json
    export DW3_MODS_DIR="$PWD/pc/build/review-mods"
    export DW3_SAVES_DIR="$PWD/pc/build/review-saves"
fi
# Every run keeps a detailed log (audio events included, SND_DEBUG) so an
# intermittent problem the owner hears can be compared against a good run.
logdir=pc/build/review-logs
mkdir -p "$logdir"
log="$logdir/$(date +%Y%m%d-%H%M%S)-$1.log"
echo "Registro de esta corrida: $log"
ls -1t "$logdir"/*.log 2>/dev/null | tail -n +21 | xargs -r rm -f
SND_DEBUG=1 ./pc/build/boot "$disc" 9999999 "$shot" 0 "$keys" >"$log" 2>&1

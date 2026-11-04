# sample-enemy: un enemigo nuevo y dónde aparece (mod de contenido)

Ejemplo mínimo de un mod `"kind": "content"` que **agrega un Digimon enemigo** con `content/digimon.json` (`digimon-v1`) y lo
**coloca en encuentros** que ya existen. No trae nada del disco: el enemigo copia una fila de la tabla de batalla y presta el
modelo de otro luchador.

## Qué hace

- `"new"."sproutmon"`: el enemigo `sample-enemy:sproutmon`. Parte de `dw3:digimon/4` (`"base"`: sus estadísticas, técnicas y
  comportamiento) y le cambia `stats.attack` a 120, `stats.defense` a 40, `stats.speed` a 70, el botín (`dw3:item/45`, siempre) y
  el nombre. `"model": "dw3:digimon/25"` hace que se vea (modelo, movimientos y efectos) como ese otro enemigo.
- `"encounters"`: en los encuentros 34 y 35 (los de la zona 330, el Parque Central) pone a ese enemigo en el segundo hueco
  (`"2"`), con nivel 3, 60 PS, 10 PM y fuerza 16. Esos encuentros eran de un solo enemigo, así que ahora pelean dos.

## Cosas a saber

- El id lo reparte el port (471 en adelante, en orden alfabético del nombre completo) y puede cambiar si instalas otros mods: por
  eso se escribe siempre por nombre, `"<carpeta del mod>:<llave>"`.
- El nombre en pantalla sale de `"name"` (`es-419` -> `spa` -> `eng`). La descripción se guarda, pero ninguna pantalla muestra la de un
  enemigo todavía.
- Sproutmon suelta su objeto desde el segundo hueco. En el juego original no caería, porque el sorteo del botín leía mal el hueco
  sorteado; el port lo corrige.
- Un hueco vacío no tiene valores que heredar: por eso el ejemplo trae `level`, `hp`, `mp` y `strength`.

## Qué pasa al quitar el mod

Nada se guarda en la partida (ningún dato guardado lleva el id de un enemigo): los encuentros vuelven a ser los originales y el
enemigo deja de existir. Detalle y límites: `docs/modding/README.md` §6.9.

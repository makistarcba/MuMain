// ============================================================================
//  Vigilante.h - VORTHANIA
//
//  Un perro guardian para los cuelgues, y una red para los crasheos.
//
//  EL PROBLEMA QUE RESUELVE
//  Cuando el cliente se tilda, el MuError.log no dice absolutamente nada: el
//  proceso sigue vivo, dibujando el ultimo cuadro, sin excepcion ni error. Y
//  cuando crashea, el log corta a mitad de linea y tampoco queda rastro de
//  donde estaba. En los dos casos uno se queda teorizando sobre codigo que a
//  lo mejor ni se estaba ejecutando.
//
//  COMO FUNCIONA
//  El hilo principal deja una marca de "en que anda" (MarcarEtapa) y late una
//  vez por cuadro (LatirBuclePrincipal). Un hilo aparte mira esos dos datos: si
//  el bucle no late por mas de cinco segundos, escribe en el log la etapa, el
//  numero de cuadro y hace cuanto que esta quieto. Con eso el proximo cuelgue
//  nos dice la zona en vez de nada.
//
//  Ademas instala un filtro de excepciones no atrapadas: si el cliente crashea,
//  deja en el log el codigo de excepcion, la direccion traducida a
//  "Main.exe+0x1234" y la etapa en la que estaba. Despues deja morir al proceso
//  normalmente, como si el filtro no estuviera.
//
//  COSTO
//  Despreciable. Por cuadro son dos escrituras atomicas relajadas. El hilo
//  vigilante duerme medio segundo entre chequeos.
// ============================================================================
#pragma once

namespace vorthania
{

// Arranca el hilo vigilante e instala el filtro de excepciones.
void IniciarVigilante();

// Para el hilo. Se puede llamar aunque nunca se haya iniciado.
void DetenerVigilante();

// Deja anotado en que anda el hilo principal.
// IMPORTANTE: solo literales de cadena. Se guarda el puntero, no una copia.
void MarcarEtapa(const char* etapa);

// Un latido por cuadro, desde el bucle principal.
void LatirBuclePrincipal();

// Un latido desde el hilo del MU Helper, para poder distinguir cual de los dos
// hilos es el que se quedo trabado.
void LatirMuHelper();

} // namespace vorthania

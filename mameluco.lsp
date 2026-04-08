;;; PATRON_MAMELUCO_SIMPLIFICADO.LSP
;;; Construye un cuadrante y usa MIRROR para generar el patrón completo
;;; Comandos: PMAMELUCO

(defun c:PMAMELUCO ( / *error* L centro capa pto-base)

  ;; Manejador de errores
  (defun *error* (msg)
    (if (and msg (not (wcmatch (strcase msg) "*CANCEL*,*EXIT*")))
      (princ (strcat "\nError: " msg))
    )
    (princ)
  )

  ;; Función para calcular punto polar
  (defun polar-pt (pt ang dist)
    (list
      (+ (car pt) (* dist (cos ang)))
      (+ (cadr pt) (* dist (sin ang)))
      0.0
    )
  )

  ;; Función para dibujar polilínea
  (defun dibujar-linea (p1 p2 capa)
    (entmake
      (list
        '(0 . "LINE")
        '(100 . "AcDbEntity")
        '(100 . "AcDbLine")
        (cons 8 capa)
        (cons 10 p1)
        (cons 11 p2)
      )
    )
  )

  ;; Función para dibujar polilínea abierta o cerrada
  (defun dibujar-polilinea (pts capa cerrar)
    (if (> (length pts) 1)
      (entmake
        (append
          (list
            '(0 . "LWPOLYLINE")
            '(100 . "AcDbEntity")
            '(100 . "AcDbPolyline")
            (cons 8 capa)
            (cons 90 (length pts))
            (cons 70 (if cerrar 1 0))
            '(43 . 0.0)
          )
          (mapcar '(lambda (p) (cons 10 p)) pts)
        )
      )
    )
  )

  ;; --- PROGRAMA PRINCIPAL ---
  (setq L (getreal "\nMódulo base [944.2]: "))
  (if (not L) (setq L 944.2))

  (setq centro (getpoint "\nPunto central del patrón: "))
  (if (not centro) (setq centro '(0 0 0)))

  (setq capa (getstring "\nCapa [arabesco]: "))
  (if (= capa "") (setq capa "arabesco"))

  (princ "\nDibujando cuadrante superior izquierdo...")

  ;; Punto base para el cuadrante (esquina superior izquierda del marco)
  (setq pto-base (list (- (car centro) (* L 1.175)) (+ (cadr centro) (* L 1.175)) 0.0))

  ;; Ángulos útiles
  (setq ang-0   0.0)
  (setq ang-45  (* pi 0.25))
  (setq ang-90  (* pi 0.5))
  (setq ang-135 (* pi 0.75))
  (setq ang-180 pi)

  ;; ==========================================
  ;; 1. MARCO EXTERIOR (cuadrante)
  ;; ==========================================
  (setq marco-pts
    (list
      pto-base
      (list (car pto-base) (- (cadr pto-base) (* L 2.35)) 0.0)
      (list (+ (car pto-base) (* L 2.35)) (- (cadr pto-base) (* L 2.35)) 0.0)
    )
  )
  (dibujar-polilinea marco-pts capa nil)

  ;; ==========================================
  ;; 2. DIAGONAL PRINCIPAL (45°)
  ;; ==========================================
  (setq p1 (list (car pto-base) (- (cadr pto-base) (* L 2.35)) 0.0))
  (setq p2 (list (+ (car pto-base) (* L 2.35)) (cadr pto-base) 0.0))
  (dibujar-linea p1 p2 capa)

  ;; ==========================================
  ;; 3. OCTÓGONO (parte del cuadrante)
  ;; ==========================================
  (setq radio-oct (* L 1.3066))
  (setq oct-pts
    (list
      (polar-pt centro ang-0 radio-oct)
      (polar-pt centro ang-45 radio-oct)
      (polar-pt centro ang-90 radio-oct)
    )
  )
  (dibujar-polilinea oct-pts capa nil)

  ;; ==========================================
  ;; 4. ESTRELLA DE 8 PUNTAS (cuadrante)
  ;; ==========================================
  (setq radio-ext (* L 0.924))
  (setq radio-int (* radio-ext 0.4142))
  
  (setq estrella-pts
    (list
      (polar-pt centro ang-0 radio-ext)      ; 0°
      (polar-pt centro ang-22.5 radio-int)   ; 22.5°
      (polar-pt centro ang-45 radio-ext)     ; 45°
      (polar-pt centro ang-67.5 radio-int)   ; 67.5°
      (polar-pt centro ang-90 radio-ext)     ; 90°
    )
  )
  (dibujar-polilinea estrella-pts capa nil)

  ;; ==========================================
  ;; 5. ROMBO INTERIOR (cuadrado a 45°)
  ;; ==========================================
  (setq radio-rombo (* L 0.6533))
  (setq rombo-pts
    (list
      (polar-pt centro ang-0 radio-rombo)
      (polar-pt centro ang-45 radio-rombo)
      (polar-pt centro ang-90 radio-rombo)
    )
  )
  (dibujar-polilinea rombo-pts capa nil)

  ;; ==========================================
  ;; 6. LÓBULO SUPERIOR (como handle 4798)
  ;; ==========================================
  (setq offset (* L 0.765))
  (setq pt-centro-lobulo (polar-pt centro ang-90 offset))
  
  (setq lobulo-pts
    (list
      (polar-pt pt-centro-lobulo ang-45 (* L 0.5))
      (polar-pt pt-centro-lobulo ang-22.5 (* L 0.707))
      pt-centro-lobulo
      (polar-pt pt-centro-lobulo (- (* 2 pi) ang-22.5) (* L 0.707))
    )
  )
  (dibujar-polilinea lobulo-pts capa nil)

  ;; ==========================================
  ;; 7. LÓBULO IZQUIERDO (como handle 4797 pero en cuadrante)
  ;; ==========================================
  (setq pt-centro-lobulo2 (polar-pt centro ang-180 offset))
  
  (setq lobulo2-pts
    (list
      (polar-pt pt-centro-lobulo2 (+ pi ang-45) (* L 0.5))
      (polar-pt pt-centro-lobulo2 (+ pi ang-22.5) (* L 0.707))
      pt-centro-lobulo2
      (polar-pt pt-centro-lobulo2 (- pi ang-22.5) (* L 0.707))
    )
  )
  (dibujar-polilinea lobulo2-pts capa nil)

  ;; ==========================================
  ;; 8. ELEMENTOS DECORATIVOS INTERIORES
  ;; ==========================================
  ;; Línea horizontal central (parte del cuadrante)
  (setq p1 (list (car centro) (+ (cadr centro) (* L 1.175)) 0.0))
  (setq p2 (list (car centro) (cadr centro) 0.0))
  (dibujar-linea p1 p2 capa)
  
  ;; Línea vertical central
  (setq p1 (list (- (car centro) (* L 1.175)) (cadr centro) 0.0))
  (setq p2 (list (car centro) (cadr centro) 0.0))
  (dibujar-linea p1 p2 capa)
  
  ;; Pequeño cuadrado en la esquina del cuadrante
  (setq esquina-offset (* L 0.425))
  (setq cuad-pts
    (list
      (list (- (car pto-base) esquina-offset) (cadr pto-base) 0.0)
      (list (car pto-base) (cadr pto-base) 0.0)
      (list (car pto-base) (- (cadr pto-base) esquina-offset) 0.0)
    )
  )
  (dibujar-polilinea cuad-pts capa nil)

  (princ "\nCuadrante dibujado. Aplicando simetrías...")

  ;; ==========================================
  ;; APLICAR MIRROR PARA GENERAR EL PATRÓN COMPLETO
  ;; ==========================================
  
  ;; Seleccionar todo lo dibujado
  (setq seleccion (ssget "X" (list (cons 8 capa))))
  
  ;; Mirror horizontal (derecha)
  (if seleccion
    (progn
      (setq p1-mirror (list (car centro) (- (cadr centro) 10000) 0.0))
      (setq p2-mirror (list (car centro) (+ (cadr centro) 10000) 0.0))
      (command "MIRROR" seleccion "" p1-mirror p2-mirror "N")
      
      ;; Seleccionar todo incluyendo el mirror
      (setq seleccion2 (ssget "X" (list (cons 8 capa))))
      
      ;; Mirror vertical (abajo)
      (setq p1-mirror2 (list (- (car centro) 10000) (cadr centro) 0.0))
      (setq p2-mirror2 (list (+ (car centro) 10000) (cadr centro) 0.0))
      (command "MIRROR" seleccion2 "" p1-mirror2 p2-mirror2 "N")
    )
  )

  (princ "\n¡Patrón mameluco completado!")
  (princ)
)

;;; Comando rápido con valores por defecto
(defun c:PMR ()
  (setq L 944.2)
  (setq centro '(0 0 0))
  (setq capa "arabesco")
  (c:PMAMELUCO)
)

(princ "\nPatrón Mameluco Simplificado cargado.")
(princ "\nComandos: PMAMELUCO (con parámetros) o PMR (rápido)")
(princ)
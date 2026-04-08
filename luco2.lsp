;;; ARABESCORL.lsp  –  Arabesco Andaluz Reticular
;;; Entrada única: longitud fundamental A
;;; Baldosa: S = A*(3 + 2*sqrt(3))  ~= 6.464*A
;;;
;;; Construccion:
;;;   kBot / kTop  : zigzag 30-45-45-30 (ancho = S exacto)
;;;   kRgt / kLft  : S-curva  30-45-45-30 (alto  = S exacto)
;;;   kMain        : 49 nodos de la trenza principal (fracciones*S)

;;; ── helper: crea LWPOLYLINE abierta en capa "arabesco" ──────────────
(defun ARB-POLY (pts / dxf)
  (setq dxf (list
    '(0 . "LWPOLYLINE")
    '(100 . "AcDbEntity")
    '(8 . "arabesco")
    '(100 . "AcDbPolyline")
    (cons 90 (length pts))
    '(70 . 0)
  ))
  (foreach p pts
    (setq dxf (append dxf (list (list 10 (car p) (cadr p))))))
  (entmake dxf)
)

;;; ── dibuja una baldosa con esquina inferior-izquierda (ox, oy) ──────
(defun ARB-TILE (ox oy A S / r3 s2 xr xl)
  (setq r3 (sqrt 3.0)
        s2 (* 0.5 S)
        ;; posicion X de las S-curvas (derivado exacto: A*(11-r3)/2 )
        xr (+ ox (* A (/ (- 11.0 r3) 2.0)))   ; derecha
        xl (- (+ ox S) (* A (/ (- 11.0 r3) 2.0)))) ; izquierda (simetrica)

  ;; ── kBot : W inferior  30°→45°→45°→30° ─────────────────────────────
  (ARB-POLY (list
    (list ox                         (+ oy (* A (/ (+ 2.0 r3) 2.0))))  ;P1
    (list (+ ox (* A r3))            (+ oy (* A (/ r3 2.0))))           ;P2
    (list (+ ox (* A (+ r3 1.5)))    (+ oy (* A (/ (+ r3 3.0) 2.0))))  ;P3
    (list (+ ox (* A (+ r3 3.0)))    (+ oy (* A (/ r3 2.0))))           ;P4
    (list (+ ox S)                   (+ oy (* A (/ (+ 2.0 r3) 2.0))))  ;P5
  ))

  ;; ── kTop : M superior  (espejo de kBot respecto y = oy+S) ───────────
  (ARB-POLY (list
    (list ox                         (- (+ oy S) (* A (/ (+ 2.0 r3) 2.0))))
    (list (+ ox (* A r3))            (- (+ oy S) (* A (/ r3 2.0))))
    (list (+ ox (* A (+ r3 1.5)))    (- (+ oy S) (* A (/ (+ r3 3.0) 2.0))))
    (list (+ ox (* A (+ r3 3.0)))    (- (+ oy S) (* A (/ r3 2.0))))
    (list (+ ox S)                   (- (+ oy S) (* A (/ (+ 2.0 r3) 2.0))))
  ))

  ;; ── kRgt : S-curva derecha  (misma x arriba y abajo) ────────────────
  ;;  P1→P2: +A,+A*r3  |  P2→P3: -1.5A,+1.5A  |  P3→P4: +1.5A,+1.5A  |  P4→P5: -A,+A*r3
  (ARB-POLY (list
    (list xr               oy)
    (list (+ xr A)         (+ oy (* A r3)))
    (list (- xr (* A 0.5)) (+ oy s2))
    (list (+ xr A)         (- (+ oy S) (* A r3)))
    (list xr               (+ oy S))
  ))

  ;; ── kLft : S-curva izquierda (espejo de kRgt) ───────────────────────
  (ARB-POLY (list
    (list xl               (+ oy S))
    (list (- xl A)         (- (+ oy S) (* A r3)))
    (list (+ xl (* A 0.5)) (+ oy s2))
    (list (- xl A)         (+ oy (* A r3)))
    (list xl               oy)
  ))

  ;; ── kMain : 49 nodos de la trenza principal (fracciones de S) ───────
  (ARB-POLY
    (mapcar (function (lambda (p)
               (list (+ ox (* (car  p) S))
                     (+ oy (* (cadr p) S)))))
      '((0.86391 1.00000)(0.82613 0.93497)(0.51952 0.93497)
        (0.43517 0.61748)(0.12052 0.70254)(0.00531 0.50129)
        (0.12052 0.30005)(0.43517 0.38505)(0.51952 0.06771)
        (0.82666 0.06771)(0.86507 0.00064)(0.91673 0.08274)
        (0.99815 0.13477)(0.93160 0.17353)(0.93160 0.48334)
        (0.61655 0.56841)(0.70119 0.88586)(0.50515 1.00000)
        (0.30221 0.88586)(0.38658 0.56841)(0.07184 0.48334)
        (0.07184 0.17353)(0.00531 0.13477)(0.08674 0.08274)
        (0.13409 0.00000)(0.17670 0.06771)(0.48392 0.06771)
        (0.56820 0.38505)(0.88299 0.30005)(0.99567 0.49692)
        (0.88299 0.70254)(0.56820 0.61748)(0.48392 0.93497)
        (0.17670 0.93497)(0.13949 1.00000)(0.08674 0.91980)
        (0.00531 0.86772)(0.07184 0.82901)(0.07184 0.51932)
        (0.38658 0.43427)(0.30221 0.11682)(0.50002 0.00000)
        (0.70052 0.11682)(0.61655 0.43427)(0.93160 0.51932)
        (0.93160 0.82901)(0.99815 0.86772)(0.91673 0.91980)
        (0.86558 1.00000))))
)

;;; ── comando principal ────────────────────────────────────────────────
(defun c:ARABESCORL (/ ipt A S cols rows i j)
  (setq ipt (getpoint "\nEsquina inferior izquierda: "))
  (if (null ipt) (exit))
  (setq A (getdist ipt "\nLongitud fundamental A (paso 30/45): "))
  (if (or (null A) (<= A 0)) (exit))
  (setq cols (getint "\nColumnas <3>: "))
  (if (null cols) (setq cols 3))
  (setq rows (getint "\nFilas    <3>: "))
  (if (null rows) (setq rows 3))
  (setq S (* A (+ 3.0 (* 2.0 (sqrt 3.0))))
        i 0)
  (repeat rows
    (setq j 0)
    (repeat cols
      (ARB-TILE (+ (car ipt)  (* j S))
                (+ (cadr ipt) (* i S))
                A S)
      (setq j (1+ j)))
    (setq i (1+ i)))
  (princ (strcat "\n" (itoa cols) "x" (itoa rows)
                 " baldosas  A=" (rtos A 2 1)
                 "  S=" (rtos S 2 1)))
  (princ)
)

;;; ARABESCOTOROSOL.lsp
;;; Arabesco Nazari en Toro 3D - VERSION SOLIDA (renderable)
;;;
;;; Cada strap es una banda con seccion rectangular:
;;;   Normal al toro:   N = (P - centro_circulo_menor) / Rs
;;;   Binormal:         B = normalize(tangente x N)
;;;   Base del strap:   P +/- ww*B
;;;   Techo del strap:  P + hh*N +/- ww*B
;;; Se generan 3DFACEs: techo + pared izq + pared der por sub-segmento.
;;;
;;; S  = A*(3+2*sqrt(3))
;;; Rb = n*S/(2*pi)   radio mayor
;;; Rs = m*S/(2*pi)   radio menor

;;; ── algebra vectorial 3D ─────────────────────────────────────────────
(defun vs+ (a b)
  (list (+ (car a)(car b)) (+ (cadr a)(cadr b)) (+ (caddr a)(caddr b))))
(defun vs- (a b)
  (list (- (car a)(car b)) (- (cadr a)(cadr b)) (- (caddr a)(caddr b))))
(defun vs* (s v)
  (list (* s (car v)) (* s (cadr v)) (* s (caddr v))))
(defun vscrs (aa bb)
  (list (- (* (cadr aa)(caddr bb)) (* (caddr aa)(cadr bb)))
        (- (* (caddr aa)(car bb))  (* (car aa)(caddr bb)))
        (- (* (car aa)(cadr bb))   (* (cadr aa)(car bb)))))
(defun vsnrm (v / vl)
  (setq vl (max 1e-12
                (sqrt (+ (* (car v)(car v))
                         (* (cadr v)(cadr v))
                         (* (caddr v)(caddr v))))))
  (list (/ (car v) vl) (/ (cadr v) vl) (/ (caddr v) vl)))

;;; ── punto 2D -> 3D en toro (coordenadas locales, origen 0,0,0) ───────
(defun solpt (wx wy Rb Rs nT mT S / th ph)
  (setq th (* wx (/ (* 2.0 pi) (* nT S)))
        ph (* wy (/ (* 2.0 pi) (* mT S))))
  (list (* (+ Rb (* Rs (cos ph))) (cos th))
        (* (+ Rb (* Rs (cos ph))) (sin th))
        (*  Rs (sin ph))))

;;; ── normal a la superficie del toro en punto local P ─────────────────
(defun solnrm (P Rb / rxy scx scy)
  (setq rxy (max 1e-12 (sqrt (+ (* (car P)(car P)) (* (cadr P)(cadr P)))))
        scx (* Rb (/ (car P) rxy))
        scy (* Rb (/ (cadr P) rxy)))
  (vsnrm (list (- (car P) scx) (- (cadr P) scy) (caddr P))))

;;; ── crea 3DFACE con aristas invisibles (render limpio) ───────────────
(defun solface (p1 p2 p3 p4 cx cy cz lyr)
  (entmake
    (list '(0 . "3DFACE")
          (cons 8 lyr)
          (cons 10 (list (+ cx (car p1)) (+ cy (cadr p1)) (+ cz (caddr p1))))
          (cons 11 (list (+ cx (car p2)) (+ cy (cadr p2)) (+ cz (caddr p2))))
          (cons 12 (list (+ cx (car p3)) (+ cy (cadr p3)) (+ cz (caddr p3))))
          (cons 13 (list (+ cx (car p4)) (+ cy (cadr p4)) (+ cz (caddr p4))))
          '(70 . 15))))

;;; ── genera techo+paredes para un sub-segmento del strap ─────────────
(defun solquad (pC pN Rb ww hh cx cy cz lyr
                / dv dlen NC NN Tc Bn
                  TL TR BL BR TLn TRn BLn BRn)
  (setq dv   (vs- pN pC)
        dlen (max 1e-12 (sqrt (+ (* (car dv)(car dv))
                                  (* (cadr dv)(cadr dv))
                                  (* (caddr dv)(caddr dv))))))
  (if (> dlen 1e-8)
    (progn
      (setq NC (solnrm pC Rb)
            NN (solnrm pN Rb)
            Tc (vsnrm dv))
      ;; esquinas en pC
      (setq Bn  (vsnrm (vscrs Tc NC))
            TL  (vs+ pC (vs+ (vs* hh NC) (vs* (- ww) Bn)))
            TR  (vs+ pC (vs+ (vs* hh NC) (vs* ww Bn)))
            BL  (vs+ pC (vs* (- ww) Bn))
            BR  (vs+ pC (vs* ww Bn)))
      ;; esquinas en pN
      (setq Bn  (vsnrm (vscrs Tc NN))
            TLn (vs+ pN (vs+ (vs* hh NN) (vs* (- ww) Bn)))
            TRn (vs+ pN (vs+ (vs* hh NN) (vs* ww Bn)))
            BLn (vs+ pN (vs* (- ww) Bn))
            BRn (vs+ pN (vs* ww Bn)))
      ;; 3 caras: techo, pared izq, pared der
      (solface TL  TR  TRn TLn cx cy cz lyr)
      (solface BL  TL  TLn BLn cx cy cz lyr)
      (solface TR  BR  BRn TRn cx cy cz lyr))))

;;; ── recorre polilinea 2D generando straps solidos ────────────────────
(defun solpoly (pts2d Rb Rs nT mT S D ww hh cx cy cz lyr
                / prev cur ii ac an wxc wyc wxn wyn)
  (setq prev (car pts2d))
  (foreach cur (cdr pts2d)
    (setq ii 0)
    (repeat D
      (setq ac  (/ (float ii)      (float D))
            an  (/ (float (1+ ii)) (float D))
            wxc (+ (car  prev) (* ac (- (car  cur) (car  prev))))
            wyc (+ (cadr prev) (* ac (- (cadr cur) (cadr prev))))
            wxn (+ (car  prev) (* an (- (car  cur) (car  prev))))
            wyn (+ (cadr prev) (* an (- (cadr cur) (cadr prev)))))
      (solquad (solpt wxc wyc Rb Rs nT mT S)
               (solpt wxn wyn Rb Rs nT mT S)
               Rb ww hh cx cy cz lyr)
      (setq ii (1+ ii)))
    (setq prev cur)))

;;; ── una baldosa solida sobre el toro ─────────────────────────────────
(defun soltile (col irow A S Rb Rs nT mT cx cy cz D ww hh lyr
                / ox oy r3 xr xl)
  (setq r3 (sqrt 3.0)
        ox (* col S)
        oy (* irow S)
        xr (+ ox (* A (* 0.5 (- 11.0 r3))))
        xl (- (+ ox S) (* A (* 0.5 (- 11.0 r3)))))

  ;; kBot: W inferior 30-45-45-30
  (solpoly
    (list (list ox                      (+ oy (* A (* 0.5 (+ 2.0 r3)))))
          (list (+ ox (* A r3))         (+ oy (* A r3 0.5)))
          (list (+ ox (* A (+ r3 1.5))) (+ oy (* A (* 0.5 (+ r3 3.0)))))
          (list (+ ox (* A (+ r3 3.0))) (+ oy (* A r3 0.5)))
          (list (+ ox S)                (+ oy (* A (* 0.5 (+ 2.0 r3))))))
    Rb Rs nT mT S D ww hh cx cy cz lyr)

  ;; kTop: M superior
  (solpoly
    (list (list ox                      (- (+ oy S) (* A (* 0.5 (+ 2.0 r3)))))
          (list (+ ox (* A r3))         (- (+ oy S) (* A r3 0.5)))
          (list (+ ox (* A (+ r3 1.5))) (- (+ oy S) (* A (* 0.5 (+ r3 3.0)))))
          (list (+ ox (* A (+ r3 3.0))) (- (+ oy S) (* A r3 0.5)))
          (list (+ ox S)                (- (+ oy S) (* A (* 0.5 (+ 2.0 r3))))))
    Rb Rs nT mT S D ww hh cx cy cz lyr)

  ;; kRgt: S-curva derecha
  (solpoly
    (list (list xr               oy)
          (list (+ xr A)         (+ oy (* A r3)))
          (list (- xr (* A 0.5)) (+ oy (* S 0.5)))
          (list (+ xr A)         (- (+ oy S) (* A r3)))
          (list xr               (+ oy S)))
    Rb Rs nT mT S D ww hh cx cy cz lyr)

  ;; kLft: S-curva izquierda
  (solpoly
    (list (list xl               (+ oy S))
          (list (- xl A)         (- (+ oy S) (* A r3)))
          (list (+ xl (* A 0.5)) (+ oy (* S 0.5)))
          (list (- xl A)         (+ oy (* A r3)))
          (list xl               oy))
    Rb Rs nT mT S D ww hh cx cy cz lyr)

  ;; kMain: 49 nodos de la trenza principal
  (solpoly
    (mapcar (function (lambda (p)
        (list (+ ox (* (car p) S)) (+ oy (* (cadr p) S)))))
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
        (0.86558 1.00000)))
    Rb Rs nT mT S D ww hh cx cy cz lyr))

;;; ── comando principal ─────────────────────────────────────────────────
(defun C:ARABESCOTOROSOL (/ ipt A nT mT D fw fh S Rb Rs ww hh
                            cx cy cz icol irow ok lyr)
  (princ "\nARABESCOTOROSOL - Arabesco Nazari Toro 3D Solido")
  (princ "\n  Straps: techo + paredes laterales sobre la superficie del toro\n")
  (setq ok T  lyr "arabesco3d")
  (setq ipt (getpoint "\nCentro del toro: "))
  (if (null ipt) (setq ok nil))
  (if ok (progn
    (setq A (getdist ipt "\nLongitud fundamental A <100>: "))
    (if (null A) (setq A 100.0))
    (if (<= A 0.0) (setq ok nil))))
  (if ok (progn
    (setq nT (getint "\nBaldosas circunferencia mayor <6>: "))
    (if (null nT) (setq nT 6))
    (if (< nT 1) (setq nT 1))
    (setq mT (getint "\nBaldosas circunferencia menor <3>: "))
    (if (null mT) (setq mT 3))
    (if (< mT 1) (setq mT 1))
    (setq D (getint "\nSubdivisiones por segmento <6>: "))
    (if (null D) (setq D 6))
    (if (< D 2) (setq D 2))
    (setq fw (getreal "\nAncho de strap, factor de A <0.28>: "))
    (if (null fw) (setq fw 0.28))
    (setq fh (getreal "\nAlto de strap, factor de A <0.08>: "))
    (if (null fh) (setq fh 0.08))
    (setq S    (* A (+ 3.0 (* 2.0 (sqrt 3.0))))
          Rb   (/ (* nT S) (* 2.0 pi))
          Rs   (/ (* mT S) (* 2.0 pi))
          ww   (* A fw 0.5)
          hh   (* A fh)
          cx   (car  ipt)
          cy   (cadr ipt)
          cz   (caddr ipt)
          irow 0)
    (repeat mT
      (setq icol 0)
      (repeat nT
        (soltile icol irow A S Rb Rs nT mT cx cy cz D ww hh lyr)
        (setq icol (1+ icol)))
      (setq irow (1+ irow)))
    (princ (strcat
      "\nToro solido: " (itoa nT) "x" (itoa mT) " baldosas"
      "  Rb=" (rtos Rb 2 0)
      "  Rs=" (rtos Rs 2 0)
      "  ancho=" (rtos (* 2.0 ww) 2 0)
      "  alto=" (rtos hh 2 0)))))
  (princ))

;;; ARABESCOHIPSOL.lsp
;;; Arabesco Nazari en Paraboloide Hiperbolico 3D (solido/renderable)
;;;
;;; Superficie sillon (saddle):
;;;   z = amp * ( (xl/Lx)^2 - (yl/Ly)^2 )
;;;   xl = wx - nT*S/2,  yl = wy - mT*S/2
;;;   Lx = nT*S/2,       Ly = mT*S/2
;;;   amp = amplitud del sillon (factor de A)
;;;
;;; S = A*(3+2*sqrt(3))
;;; Normal: normalize( -2*amp*xl/Lx^2,  2*amp*yl/Ly^2,  1 )
;;;
;;; Genera 3DFACEs: techo + pared izq + pared der por sub-segmento.
;;; Parametros del strap: ww (semi-ancho) y hs (altura seccion).

;;; ── algebra vectorial 3D ─────────────────────────────────────────────
(defun vs+ (aa bb)
  (list (+ (car aa)(car bb)) (+ (cadr aa)(cadr bb)) (+ (caddr aa)(caddr bb))))
(defun vs- (aa bb)
  (list (- (car aa)(car bb)) (- (cadr aa)(cadr bb)) (- (caddr aa)(caddr bb))))
(defun vs* (ss vv)
  (list (* ss (car vv)) (* ss (cadr vv)) (* ss (caddr vv))))
(defun vscrs (aa bb)
  (list (- (* (cadr aa)(caddr bb)) (* (caddr aa)(cadr bb)))
        (- (* (caddr aa)(car bb))  (* (car aa)(caddr bb)))
        (- (* (car aa)(cadr bb))   (* (cadr aa)(car bb)))))
(defun vsnrm (vv / vl)
  (setq vl (max 1e-12 (sqrt (+ (* (car vv)(car vv))
                               (* (cadr vv)(cadr vv))
                               (* (caddr vv)(caddr vv))))))
  (list (/ (car vv) vl) (/ (cadr vv) vl) (/ (caddr vv) vl)))

;;; ── punto 2D -> 3D en paraboloide hiperbolico (coord. locales) ──────
;;; wx wy en espacio de la baldosa 2D;  nT mT S amp definen la superficie
(defun hppt (wx wy nT mT S amp / lx ly xl yl)
  (setq lx (* 0.5 nT S)
        ly (* 0.5 mT S)
        xl (- wx lx)
        yl (- wy ly))
  (list xl yl (* amp (- (* (/ xl lx)(/ xl lx))
                        (* (/ yl ly)(/ yl ly))))))

;;; ── normal al paraboloide en punto local P ──────────────────────────
;;; F = z - amp*(xl/Lx)^2 + amp*(yl/Ly)^2 = 0
;;; grad F = ( -2*amp*xl/Lx^2,  2*amp*yl/Ly^2,  1 )
(defun hpnrm (P nT mT S amp / lx ly gx gy)
  (setq lx (* 0.5 nT S)
        ly (* 0.5 mT S)
        gx (* -2.0 amp (/ (car  P) (* lx lx)))
        gy (*  2.0 amp (/ (cadr P) (* ly ly))))
  (vsnrm (list gx gy 1.0)))

;;; ── crea 3DFACE con aristas invisibles (render limpio) ───────────────
(defun hpface (p1 p2 p3 p4 cx cy cz lyr)
  (entmake
    (list '(0 . "3DFACE")
          (cons 8 lyr)
          (cons 10 (list (+ cx (car p1)) (+ cy (cadr p1)) (+ cz (caddr p1))))
          (cons 11 (list (+ cx (car p2)) (+ cy (cadr p2)) (+ cz (caddr p2))))
          (cons 12 (list (+ cx (car p3)) (+ cy (cadr p3)) (+ cz (caddr p3))))
          (cons 13 (list (+ cx (car p4)) (+ cy (cadr p4)) (+ cz (caddr p4))))
          '(70 . 15))))

;;; ── genera techo+paredes para un sub-segmento del strap ─────────────
(defun hpquad (pC pN nT mT S amp ww hs cx cy cz lyr
               / dv dlen NC NN Tc Bn
                 TL TR BL BR TLn TRn BLn BRn)
  (setq dv   (vs- pN pC)
        dlen (max 1e-12 (sqrt (+ (* (car dv)(car dv))
                                 (* (cadr dv)(cadr dv))
                                 (* (caddr dv)(caddr dv))))))
  (if (> dlen 1e-8)
    (progn
      (setq NC (hpnrm pC nT mT S amp)
            NN (hpnrm pN nT mT S amp)
            Tc (vsnrm dv))
      ;; esquinas en pC
      (setq Bn  (vsnrm (vscrs Tc NC))
            TL  (vs+ pC (vs+ (vs* hs NC) (vs* (- ww) Bn)))
            TR  (vs+ pC (vs+ (vs* hs NC) (vs* ww Bn)))
            BL  (vs+ pC (vs* (- ww) Bn))
            BR  (vs+ pC (vs* ww Bn)))
      ;; esquinas en pN
      (setq Bn  (vsnrm (vscrs Tc NN))
            TLn (vs+ pN (vs+ (vs* hs NN) (vs* (- ww) Bn)))
            TRn (vs+ pN (vs+ (vs* hs NN) (vs* ww Bn)))
            BLn (vs+ pN (vs* (- ww) Bn))
            BRn (vs+ pN (vs* ww Bn)))
      ;; 3 caras: techo, pared izq, pared der
      (hpface TL  TR  TRn TLn cx cy cz lyr)
      (hpface BL  TL  TLn BLn cx cy cz lyr)
      (hpface TR  BR  BRn TRn cx cy cz lyr))))

;;; ── recorre polilinea 2D generando straps sobre el paraboloide ──────
(defun hppoly (pts2d nT mT S amp ww hs D cx cy cz lyr
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
      (hpquad (hppt wxc wyc nT mT S amp)
              (hppt wxn wyn nT mT S amp)
              nT mT S amp ww hs cx cy cz lyr)
      (setq ii (1+ ii)))
    (setq prev cur)))

;;; ── una baldosa sobre el paraboloide ────────────────────────────────
(defun hptile (col irow A S nT mT amp ww hs D cx cy cz lyr
               / ox oy r3 xr xl)
  (setq r3 (sqrt 3.0)
        ox (* col S)
        oy (* irow S)
        xr (+ ox (* A (* 0.5 (- 11.0 r3))))
        xl (- (+ ox S) (* A (* 0.5 (- 11.0 r3)))))

  ;; kBot: W inferior  30-45-45-30
  (hppoly
    (list (list ox                      (+ oy (* A (* 0.5 (+ 2.0 r3)))))
          (list (+ ox (* A r3))         (+ oy (* A r3 0.5)))
          (list (+ ox (* A (+ r3 1.5))) (+ oy (* A (* 0.5 (+ r3 3.0)))))
          (list (+ ox (* A (+ r3 3.0))) (+ oy (* A r3 0.5)))
          (list (+ ox S)                (+ oy (* A (* 0.5 (+ 2.0 r3))))))
    nT mT S amp ww hs D cx cy cz lyr)

  ;; kTop: M superior
  (hppoly
    (list (list ox                      (- (+ oy S) (* A (* 0.5 (+ 2.0 r3)))))
          (list (+ ox (* A r3))         (- (+ oy S) (* A r3 0.5)))
          (list (+ ox (* A (+ r3 1.5))) (- (+ oy S) (* A (* 0.5 (+ r3 3.0)))))
          (list (+ ox (* A (+ r3 3.0))) (- (+ oy S) (* A r3 0.5)))
          (list (+ ox S)                (- (+ oy S) (* A (* 0.5 (+ 2.0 r3))))))
    nT mT S amp ww hs D cx cy cz lyr)

  ;; kRgt: S-curva derecha
  (hppoly
    (list (list xr               oy)
          (list (+ xr A)         (+ oy (* A r3)))
          (list (- xr (* A 0.5)) (+ oy (* S 0.5)))
          (list (+ xr A)         (- (+ oy S) (* A r3)))
          (list xr               (+ oy S)))
    nT mT S amp ww hs D cx cy cz lyr)

  ;; kLft: S-curva izquierda
  (hppoly
    (list (list xl               (+ oy S))
          (list (- xl A)         (- (+ oy S) (* A r3)))
          (list (+ xl (* A 0.5)) (+ oy (* S 0.5)))
          (list (- xl A)         (+ oy (* A r3)))
          (list xl               oy))
    nT mT S amp ww hs D cx cy cz lyr)

  ;; kMain: 49 nodos de la trenza principal
  (hppoly
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
    nT mT S amp ww hs D cx cy cz lyr))

;;; ── comando principal ────────────────────────────────────────────────
(defun C:ARABESCOHIPSOL (/ ipt A nT mT fa D fw fh S amp ww hs
                            cx cy cz icol irow ok lyr)
  (princ "\nARABESCOHIPSOL - Arabesco Nazari Paraboloide Hiperbolico Solido")
  (princ "\n  Sillon: z = amp*((x/Lx)^2 - (y/Ly)^2)  Lx=nT*S/2  Ly=mT*S/2\n")
  (setq ok T  lyr "arabesco3d")
  (setq ipt (getpoint "\nCentro del sillon: "))
  (if (null ipt) (setq ok nil))
  (if ok (progn
    (setq A (getdist ipt "\nLongitud fundamental A <100>: "))
    (if (null A) (setq A 100.0))
    (if (<= A 0.0) (setq ok nil))))
  (if ok (progn
    (setq nT (getint "\nBaldosas en X <4>: "))
    (if (null nT) (setq nT 4))
    (if (< nT 1) (setq nT 1))
    (setq mT (getint "\nBaldosas en Y <4>: "))
    (if (null mT) (setq mT 4))
    (if (< mT 1) (setq mT 1))
    (setq fa (getreal "\nAmplitud del sillon, factor de A <3.0>: "))
    (if (null fa) (setq fa 3.0))
    (setq D (getint "\nSubdivisiones por segmento <6>: "))
    (if (null D) (setq D 6))
    (if (< D 2) (setq D 2))
    (setq fw (getreal "\nAncho de strap, factor de A <0.28>: "))
    (if (null fw) (setq fw 0.28))
    (setq fh (getreal "\nAlto de strap, factor de A <0.08>: "))
    (if (null fh) (setq fh 0.08))
    (setq S   (* A (+ 3.0 (* 2.0 (sqrt 3.0))))
          amp (* A fa)
          ww  (* A fw 0.5)
          hs  (* A fh)
          cx  (car  ipt)
          cy  (cadr ipt)
          cz  (caddr ipt)
          irow 0)
    (repeat mT
      (setq icol 0)
      (repeat nT
        (hptile icol irow A S nT mT amp ww hs D cx cy cz lyr)
        (setq icol (1+ icol)))
      (setq irow (1+ irow)))
    (princ (strcat
      "\nParaboloide: " (itoa nT) "x" (itoa mT) " baldosas"
      "  S=" (rtos S 2 0)
      "  amp=" (rtos amp 2 0)
      "  ancho=" (rtos (* 2.0 ww) 2 0)
      "  alto=" (rtos hs 2 0)))))
  (princ))

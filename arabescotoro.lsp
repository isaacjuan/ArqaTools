;;; ARABESCOTORO.lsp
;;; Arabesco Nazari sobre Toro 3D
;;; S = A*(3+2*sqrt(3))  Rb=n*S/(2*pi)  Rs=m*S/(2*pi)
;;; Rb = radio mayor,  Rs = radio menor  (case-safe: no R vs r)

;;; punto plano (wx,wy) -> 3D en toro
(defun AT-PT (wx wy Rb Rs nT mT S / th ph)
  (setq th (* wx (/ (* 2.0 pi) (* nT S)))
        ph (* wy (/ (* 2.0 pi) (* mT S))))
  (list (* (+ Rb (* Rs (cos ph))) (cos th))
        (* (+ Rb (* Rs (cos ph))) (sin th))
        (*  Rs (sin ph)))
)

;;; polilinea 3D con D subdivisiones por segmento
(defun AT-POLY (pts2d Rb Rs nT mT S D cx cy cz
                / prev cur ii alpha wx wy p3)
  (entmake (list '(0 . "POLYLINE") '(8 . "arabesco3d") '(66 . 1) '(70 . 8)))
  (setq prev (car pts2d))
  (setq p3 (AT-PT (car prev) (cadr prev) Rb Rs nT mT S))
  (entmake (list '(0 . "VERTEX") '(8 . "arabesco3d")
    (list 10 (+ cx (car p3)) (+ cy (cadr p3)) (+ cz (caddr p3)))
    '(70 . 32)))
  (foreach cur (cdr pts2d)
    (setq ii 1)
    (repeat (1- D)
      (setq alpha (/ (float ii) (float D))
            wx (+ (car  prev) (* alpha (- (car  cur) (car  prev))))
            wy (+ (cadr prev) (* alpha (- (cadr cur) (cadr prev)))))
      (setq p3 (AT-PT wx wy Rb Rs nT mT S))
      (entmake (list '(0 . "VERTEX") '(8 . "arabesco3d")
        (list 10 (+ cx (car p3)) (+ cy (cadr p3)) (+ cz (caddr p3)))
        '(70 . 32)))
      (setq ii (1+ ii)))
    (setq p3 (AT-PT (car cur) (cadr cur) Rb Rs nT mT S))
    (entmake (list '(0 . "VERTEX") '(8 . "arabesco3d")
      (list 10 (+ cx (car p3)) (+ cy (cadr p3)) (+ cz (caddr p3)))
      '(70 . 32)))
    (setq prev cur))
  (entmake (list '(0 . "SEQEND") '(8 . "arabesco3d")))
)

;;; una baldosa completa sobre el toro
(defun AT-TILE (col irow A S Rb Rs nT mT cx cy cz D / ox oy r3 xr xl)
  (setq r3 (sqrt 3.0)
        ox (* col S)
        oy (* irow S)
        xr (+ ox (* A (* 0.5 (- 11.0 r3))))
        xl (- (+ ox S) (* A (* 0.5 (- 11.0 r3)))))

  ;; kBot: W inferior 30-45-45-30
  (AT-POLY
    (list (list ox                      (+ oy (* A (* 0.5 (+ 2.0 r3)))))
          (list (+ ox (* A r3))         (+ oy (* A r3 0.5)))
          (list (+ ox (* A (+ r3 1.5))) (+ oy (* A (* 0.5 (+ r3 3.0)))))
          (list (+ ox (* A (+ r3 3.0))) (+ oy (* A r3 0.5)))
          (list (+ ox S)                (+ oy (* A (* 0.5 (+ 2.0 r3))))))
    Rb Rs nT mT S D cx cy cz)

  ;; kTop: M superior
  (AT-POLY
    (list (list ox                      (- (+ oy S) (* A (* 0.5 (+ 2.0 r3)))))
          (list (+ ox (* A r3))         (- (+ oy S) (* A r3 0.5)))
          (list (+ ox (* A (+ r3 1.5))) (- (+ oy S) (* A (* 0.5 (+ r3 3.0)))))
          (list (+ ox (* A (+ r3 3.0))) (- (+ oy S) (* A r3 0.5)))
          (list (+ ox S)                (- (+ oy S) (* A (* 0.5 (+ 2.0 r3))))))
    Rb Rs nT mT S D cx cy cz)

  ;; kRgt: S-curva derecha
  (AT-POLY
    (list (list xr               oy)
          (list (+ xr A)         (+ oy (* A r3)))
          (list (- xr (* A 0.5)) (+ oy (* S 0.5)))
          (list (+ xr A)         (- (+ oy S) (* A r3)))
          (list xr               (+ oy S)))
    Rb Rs nT mT S D cx cy cz)

  ;; kLft: S-curva izquierda
  (AT-POLY
    (list (list xl               (+ oy S))
          (list (- xl A)         (- (+ oy S) (* A r3)))
          (list (+ xl (* A 0.5)) (+ oy (* S 0.5)))
          (list (- xl A)         (+ oy (* A r3)))
          (list xl               oy))
    Rb Rs nT mT S D cx cy cz)

  ;; kMain: 49 nodos de la trenza principal
  (AT-POLY
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
        (0.86558 1.00000)))
    Rb Rs nT mT S D cx cy cz)
)

;;; comando principal
(defun C:ARABESCOTORO (/ ipt A nT mT D S Rb Rs cx cy cz icol irow ok)
  (princ "\nARABESCOTORO - Arabesco Nazari sobre Toro 3D")
  (princ "\n  S=A*(3+2*sqrt(3))  Rb=n*S/(2*pi)  Rs=m*S/(2*pi)\n")
  (setq ok T)
  (setq ipt (getpoint "\nCentro del toro: "))
  (if (null ipt) (setq ok nil))
  (if ok
    (progn
      (setq A (getdist ipt "\nLongitud fundamental A <100>: "))
      (if (null A) (setq A 100.0))
      (if (<= A 0.0) (setq ok nil))
    )
  )
  (if ok
    (progn
      (setq nT (getint "\nBaldosas circunferencia mayor <6>: "))
      (if (null nT) (setq nT 6))
      (if (< nT 1) (setq nT 1))
      (setq mT (getint "\nBaldosas circunferencia menor <3>: "))
      (if (null mT) (setq mT 3))
      (if (< mT 1) (setq mT 1))
      (setq D (getint "\nSubdivisiones por segmento <8>: "))
      (if (null D) (setq D 8))
      (if (< D 2) (setq D 2))
      (setq S  (* A (+ 3.0 (* 2.0 (sqrt 3.0))))
            Rb (/ (* nT S) (* 2.0 pi))
            Rs (/ (* mT S) (* 2.0 pi))
            cx (car  ipt)
            cy (cadr ipt)
            cz (caddr ipt)
            irow 0)
      (repeat mT
        (setq icol 0)
        (repeat nT
          (AT-TILE icol irow A S Rb Rs nT mT cx cy cz D)
          (setq icol (1+ icol)))
        (setq irow (1+ irow)))
      (princ (strcat
        "\nToro: " (itoa nT) "x" (itoa mT) " baldosas"
        "  A=" (rtos A 2 0)
        "  S=" (rtos S 2 0)
        "  Rb=" (rtos Rb 2 0)
        "  Rs=" (rtos Rs 2 0)))
    )
  )
  (princ)
)

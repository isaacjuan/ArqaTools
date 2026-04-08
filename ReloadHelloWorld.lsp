;; ReloadHelloWorld.lsp
;; Quick reload command for HelloWorld.arx plugin
;; Load this file in AutoCAD Startup Suite for automatic loading

;; Configuration - Set your project path here
(setq *hw-project-path* "c:\\HSBCAD\\HelloWorldAcad2025")

(defun C:RELOADHW ( / arx-name arx-path1 arx-path2 arx-path loaded-path)
  (setq arx-name "HelloWorld.arx")
  
  ;; Define possible plugin locations
  ;; Prefer Documents location (arx-path2) since it updates during build even when loaded
  (setq arx-path1 (strcat (getenv "USERPROFILE") "\\Documents\\HelloWorld.arx"))
  (setq arx-path2 (strcat (getenv "USERPROFILE") "\\Documents\\acadPlugins\\HelloWorld.arx"))
  
  (princ "\n=== Reload HelloWorld Plugin ===")
  
  ;; Check if plugin is currently loaded
  (setq loaded-path nil)
  (if (member arx-name (arx))
    (progn
      ;; Try to find which path it's loaded from - prefer arx-path1 (Documents)
      (if (findfile arx-path1)
        (setq loaded-path arx-path1)
        (if (findfile arx-path2)
          (setq loaded-path arx-path2)
        )
      )
      
      ;; Unload the plugin
      (princ "\nUnloading HelloWorld.arx...")
      (if (arxunload arx-name T)  ;; T forces unload
        (princ " Done.")
        (princ " FAILED!")
      )
    )
    (princ "\nHelloWorld.arx not currently loaded.")
  )
  
  ;; Determine which path to load from
  (if (not loaded-path)
    (progn
      ;; Find the newest file by comparing timestamps
      (setq file1-exists (findfile arx-path1))
      (setq file2-exists (findfile arx-path2))
      
      (princ "\n--- Checking file timestamps ---")
      
      (cond
        ;; Both exist - compare timestamps and use newest
        ((and file1-exists file2-exists)
         (setq time1 (vl-file-systime arx-path1))
         (setq time2 (vl-file-systime arx-path2))
         
         ;; Display timestamps for both files in readable format
         ;; Format: (year month day-of-week day hours minutes seconds milliseconds)
         (princ (strcat "\nFile 1: " arx-path1))
         (princ (strcat "\n  Date: " 
                       (itoa (nth 0 time1)) "-" 
                       (if (< (nth 1 time1) 10) "0" "") (itoa (nth 1 time1)) "-"
                       (if (< (nth 3 time1) 10) "0" "") (itoa (nth 3 time1))
                       " Time: "
                       (if (< (nth 4 time1) 10) "0" "") (itoa (nth 4 time1)) ":"
                       (if (< (nth 5 time1) 10) "0" "") (itoa (nth 5 time1)) ":"
                       (if (< (nth 6 time1) 10) "0" "") (itoa (nth 6 time1))))
         (princ (strcat "\nFile 2: " arx-path2))
         (princ (strcat "\n  Date: " 
                       (itoa (nth 0 time2)) "-" 
                       (if (< (nth 1 time2) 10) "0" "") (itoa (nth 1 time2)) "-"
                       (if (< (nth 3 time2) 10) "0" "") (itoa (nth 3 time2))
                       " Time: "
                       (if (< (nth 4 time2) 10) "0" "") (itoa (nth 4 time2)) ":"
                       (if (< (nth 5 time2) 10) "0" "") (itoa (nth 5 time2)) ":"
                       (if (< (nth 6 time2) 10) "0" "") (itoa (nth 6 time2))))
         
         ;; Compare timestamps properly: year, month, day, hour, minute, second
         ;; time format: (year month day hours minutes seconds milliseconds)
         (setq newer1 nil)
         (setq i 0)
         (while (and (< i 6) (not newer1) (= (nth i time1) (nth i time2)))
           (setq i (1+ i))
         )
         (if (< i 6)
           (setq newer1 (> (nth i time1) (nth i time2)))
           (setq newer1 T)  ; Equal times, prefer file1
         )
         
         (if newer1
           (progn
             (setq loaded-path arx-path1)
             (princ "\n>>> SELECTED: File 1 (newer)")
           )
           (progn
             (setq loaded-path arx-path2)
             (princ "\n>>> SELECTED: File 2 (newer)")
           )
         )
        )
        ;; Only file1 exists
        (file1-exists
         (setq loaded-path arx-path1)
         (princ (strcat "\nOnly found: " arx-path1))
        )
        ;; Only file2 exists
        (file2-exists
         (setq loaded-path arx-path2)
         (princ (strcat "\nOnly found: " arx-path2))
        )
      )
    )
  )
  
  ;; Load the plugin
  (if loaded-path
    (progn
      (princ (strcat "\nLoading from: " loaded-path))
      (if (arxload loaded-path)
        (progn
          (princ "\nHelloWorld.arx reloaded successfully!")
          (command "HWVERSION")
        )
        (princ "\nERROR: Failed to load HelloWorld.arx")
      )
    )
    (progn
      (princ "\nERROR: Cannot find HelloWorld.arx in:")
      (princ (strcat "\n  " arx-path1))
      (princ (strcat "\n  " arx-path2))
      (princ "\nPlease check the plugin location.")
    )
  )
  
  (princ)
)

;; UNLOADHW - Just unload HelloWorld.arx
(defun C:UNLOADHW ( / arx-name)
  (setq arx-name "HelloWorld.arx")
  
  (princ "\n=== Unload HelloWorld Plugin ===")
  
  ;; Check if plugin is currently loaded
  (if (member arx-name (arx))
    (progn
      (princ "\nUnloading HelloWorld.arx...")
      (if (arxunload arx-name T)  ;; T forces unload
        (princ " Done.\nHelloWorld.arx unloaded successfully!")
        (princ " FAILED!\nERROR: Could not unload HelloWorld.arx")
      )
    )
    (princ "\nHelloWorld.arx is not currently loaded.")
  )
  
  (princ)
)

;; RELOADHWBUILD - Unload, Build, and Reload
(defun C:RELOADHWBUILD ( / arx-name build-bat arx-path1 arx-path2 loaded-path)
  (setq arx-name "HelloWorld.arx")
  (setq build-bat (strcat *hw-project-path* "\\Build.bat"))
  
  ;; Define possible plugin locations - prefer Documents (arx-path1)
  (setq arx-path1 (strcat (getenv "USERPROFILE") "\\Documents\\HelloWorld.arx"))
  (setq arx-path2 (strcat (getenv "USERPROFILE") "\\Documents\\acadPlugins\\HelloWorld.arx"))
  
  (princ "\n=== Build and Reload HelloWorld Plugin ===")
  
  ;; Check if Build.bat exists
  (if (not (findfile build-bat))
    (progn
      (princ (strcat "\nERROR: Cannot find Build.bat at: " build-bat))
      (princ "\nPlease update *hw-project-path* in ReloadHelloWorld.lsp")
      (princ)
      (exit)
    )
  )
  
  ;; Unload if currently loaded
  (if (member arx-name (arx))
    (progn
      (princ "\nUnloading current version...")
      (if (arxunload arx-name T)
        (princ " Done.")
        (princ " FAILED!")
      )
    )
  )
  
  ;; Trigger build and wait
  (princ "\nStarting build process...")
  (princ "\nPlease wait for build to complete...")
  
  ;; Use startapp to run Build.bat - /WAIT makes cmd wait for completion
  (startapp "cmd" (strcat "/c \"" build-bat "\" && pause"))
  
  (princ "\n\nBuild process started in separate window.")
  (princ "\nPress Enter after build completes to reload...")
  (getstring "\n")
  
  ;; Determine which path to load from (compare timestamps - use newest)
  (setq file1-exists (findfile arx-path1))
  (setq file2-exists (findfile arx-path2))
  
  (princ "\n--- Checking file timestamps ---")
  
  (cond
    ;; Both exist - compare timestamps and use newest
    ((and file1-exists file2-exists)
     (setq time1 (vl-file-systime arx-path1))
     (setq time2 (vl-file-systime arx-path2))
     
     ;; Display timestamps for both files in readable format
     ;; Format: (year month day-of-week day hours minutes seconds milliseconds)
     (princ (strcat "\nFile 1: " arx-path1))
     (princ (strcat "\n  Date: " 
                   (itoa (nth 0 time1)) "-" 
                   (if (< (nth 1 time1) 10) "0" "") (itoa (nth 1 time1)) "-"
                   (if (< (nth 3 time1) 10) "0" "") (itoa (nth 3 time1))
                   " Time: "
                   (if (< (nth 4 time1) 10) "0" "") (itoa (nth 4 time1)) ":"
                   (if (< (nth 5 time1) 10) "0" "") (itoa (nth 5 time1)) ":"
                   (if (< (nth 6 time1) 10) "0" "") (itoa (nth 6 time1))))
     (princ (strcat "\nFile 2: " arx-path2))
     (princ (strcat "\n  Date: " 
                   (itoa (nth 0 time2)) "-" 
                   (if (< (nth 1 time2) 10) "0" "") (itoa (nth 1 time2)) "-"
                   (if (< (nth 3 time2) 10) "0" "") (itoa (nth 3 time2))
                   " Time: "
                   (if (< (nth 4 time2) 10) "0" "") (itoa (nth 4 time2)) ":"
                   (if (< (nth 5 time2) 10) "0" "") (itoa (nth 5 time2)) ":"
                   (if (< (nth 6 time2) 10) "0" "") (itoa (nth 6 time2))))
     
     ;; Compare timestamps properly: year, month, day, hour, minute, second
     ;; time format: (year month day hours minutes seconds milliseconds)
     (setq newer1 nil)
     (setq i 0)
     (while (and (< i 6) (not newer1) (= (nth i time1) (nth i time2)))
       (setq i (1+ i))
     )
     (if (< i 6)
       (setq newer1 (> (nth i time1) (nth i time2)))
       (setq newer1 T)  ; Equal times, prefer file1
     )
     
     (if newer1
       (progn
         (setq loaded-path arx-path1)
         (princ "\n>>> SELECTED: File 1 (newer)")
       )
       (progn
         (setq loaded-path arx-path2)
         (princ "\n>>> SELECTED: File 2 (newer)")
       )
     )
    )
    ;; Only file1 exists
    (file1-exists
     (setq loaded-path arx-path1)
     (princ (strcat "\nOnly found: " arx-path1))
    )
    ;; Only file2 exists
    (file2-exists
     (setq loaded-path arx-path2)
     (princ (strcat "\nOnly found: " arx-path2))
    )
  )
  
  ;; Load the newly built plugin
  (if loaded-path
    (progn
      (princ (strcat "\nLoading from: " loaded-path))
      (if (arxload loaded-path)
        (progn
          (princ "\n\nHelloWorld.arx reloaded successfully!")
          (command "HWVERSION")
        )
        (princ "\n\nERROR: Failed to load HelloWorld.arx")
      )
    )
    (progn
      (princ "\nERROR: Cannot find HelloWorld.arx")
      (princ (strcat "\n  Searched: " arx-path1))
      (princ (strcat "\n  Searched: " arx-path2))
    )
  )
  
  (princ)
)

;; Alternative command with explicit path parameter
(defun C:RELOADHWPATH ( / arx-path user-path)
  (princ "\n=== Reload HelloWorld with Custom Path ===")
  (setq user-path (getfiled "Select HelloWorld.arx" "" "arx" 0))
  
  (if user-path
    (progn
      ;; Unload if loaded
      (if (member "HelloWorld.arx" (arx))
        (progn
          (princ "\nUnloading current version...")
          (arxunload "HelloWorld.arx" T)
        )
      )
      
      ;; Load from specified path
      (princ (strcat "\nLoading from: " user-path))
      (if (arxload user-path)
        (progn
          (princ "\nHelloWorld.arx loaded successfully!")
          (command "HWVERSION")
        )
        (princ "\nERROR: Failed to load HelloWorld.arx")
      )
    )
    (princ "\nCancelled.")
  )
  (princ)
)

;; Auto-load HelloWorld.arx on startup (optional - uncomment to enable)
;; (defun S::STARTUP ()
;;   (if (not (member "HelloWorld.arx" (arx)))
;;     (progn
;;       (setq hw-path (strcat (getenv "USERPROFILE") "\\Documents\\acadPlugins\\HelloWorld.arx"))
;;       (if (findfile hw-path)
;;         (progn
;;           (princ "\nAuto-loading HelloWorld.arx...")
;;           (arxload hw-path)
;;         )
;;       )
;;     )
;;   )
;; )

(princ "\n=== HelloWorld Reload Commands Loaded ===")
(princ "\nType RELOADHW to reload HelloWorld.arx")
(princ "\nType UNLOADHW to unload HelloWorld.arx (without reloading)")
(princ "\nType RELOADHWBUILD to rebuild and reload (unload -> build -> reload)")
(princ "\nType RELOADHWPATH to reload from custom location")
(princ "\n")
(princ "\nNote: Update *hw-project-path* in this file if your project is elsewhere")
(princ (strcat "\nCurrent project path: " *hw-project-path*))
(princ)

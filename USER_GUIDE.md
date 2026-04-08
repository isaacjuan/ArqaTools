# HelloWorld AutoCAD Plugin - User Guide

## Quick Start

After loading the plugin, type `HWHELP` in AutoCAD to see all available commands, or type `HWVERSION` to check the plugin version.

---

## Command Reference

### SEQNUM - Create Numbered Sequence

Creates a sequence of numbered circles with text labels inside. Perfect for numbering elements, creating coordinate markers, or labeling points in a drawing.

#### How to Use:

1. **Type `SEQNUM`** in the AutoCAD command line

2. **Pick the first point** - Click where you want the first circle to appear

3. **Pick the second point** - Click to define the spacing and direction
   - The distance between points determines spacing
   - The direction from first to second point determines the sequence direction

4. **Set text height** - Choose one of three methods:
   - **Type a number** (e.g., `2.5`) and press Enter
   - **Click two points** to visually define the height
   - **Press Enter** to use the default height (2.5 units)

5. **Enter starting number** (e.g., `1`, `100`, `-5`)
   - Can be positive or negative
   - Press Enter for default (1)

6. **Enter increment** (e.g., `1`, `2`, `5`, `-1`)
   - Use positive numbers to count up (1, 2, 3...)
   - Use negative numbers to count down (10, 9, 8...)
   - Press Enter for default (1)

7. **Enter quantity** - How many circles to create
   - Press Enter for default (10)

#### Examples:

**Example 1: Standard Numbering**
- First point: Click at (0,0)
- Second point: Click at (10,0)
- Text height: Press Enter (uses 2.5)
- Starting number: `1`
- Increment: `1`
- Quantity: `5`
- **Result:** Creates circles numbered 1, 2, 3, 4, 5 spaced 10 units apart horizontally

**Example 2: Grid Coordinates**
- First point: Click at starting corner
- Second point: Click 5 units to the right
- Text height: Click two points to match existing text
- Starting number: `100`
- Increment: `5`
- Quantity: `8`
- **Result:** Creates circles numbered 100, 105, 110, 115, 120, 125, 130, 135

**Example 3: Countdown Sequence**
- First point: Click at top position
- Second point: Click below (vertical spacing)
- Text height: Type `3`
- Starting number: `10`
- Increment: `-1`
- Quantity: `10`
- **Result:** Creates circles numbered 10, 9, 8, 7, 6, 5, 4, 3, 2, 1

#### Technical Details:

- **Circle Size:** Automatically sized using golden ratio (1.618 × text height)
- **Text Style:** Uses current AutoCAD text style
- **Grouping:** Each circle and its text are grouped together as `SEQNUM_1`, `SEQNUM_2`, etc.
- **Layer:** Created on the current active layer

#### Tips:

- **Visual Height Selection:** When setting text height, clicking two points lets you match existing text sizes visually
- **Direction Control:** The sequence follows the direction from the first point to the second point
- **Spacing Control:** The distance between your first and second points becomes the spacing for all elements
- **Group Management:** Elements are grouped for easy selection - click once to select circle and text together
- **Undo:** Use AutoCAD's `U` command to undo if you need to adjust parameters

---

## Area Tools

### INSERTAREA - Auto-Updating Area Label

Inserts a text label showing the area of a closed polyline. The text automatically updates when the polyline is modified, moved, stretched, or resized.

#### How to Use:

1. **Type `INSERTAREA`** in the AutoCAD command line

2. **Select a closed polyline** - Click on any closed polyline

#### What It Does:

- Calculates the area of the polyline
- Places a text label at the center of the polyline showing the area value
- **Formats with thousands separator** (e.g., "1,234.56" instead of "1234.56")
- **Adds unit suffix** based on drawing units (m², ft², cm², etc.)
- Links the text to the polyline using a reactor (event handler)
- **Automatic Updates:** When you modify the polyline, the area text updates automatically
- **Auto-Delete:** If you delete the polyline, the text is deleted too

#### Examples:

**Example 1: Room Area Label**
- Draw a closed rectangular polyline representing a room
- Type `INSERTAREA`
- Select the polyline
- **Result:** Text appears in the center showing "150.00 m²" (or ft² depending on your drawing units)
- Stretch the polyline - the text updates automatically with the new area!

**Example 2: Plot Boundary**
- Draw a closed polyline around a property boundary (e.g., 5,280 sq ft)
- Type `INSERTAREA`
- Select the boundary
- **Result:** Area label appears showing "5,280.00 ft²" with proper thousands separator
- If you adjust vertices, the area recalculates immediately

#### Technical Details:

- **Text Position:** Placed at the approximate center (centroid) of the polyline
- **Text Height:** Default 2.5 units
- **Text Format:** Decimal with 2 decimal places and thousands separator (e.g., "1,234.56 m²")
- **Unit Detection:** Automatically detects drawing units (INSUNITS) and adds appropriate suffix:
  - Metric: mm², cm², m², km²
  - Imperial: in², ft², yd², mi²
  - Generic: units² (if units are undefined)
- **Text Alignment:** Center-middle justified
- **Auto-Update:** Uses ObjectARX reactor to monitor polyline changes
- **Requirement:** Polyline MUST be closed (use PEDIT to close if needed)

#### Important Notes:

- **Closed Polylines Only:** The polyline must be closed. If not, you'll see an error message.
- **Real-Time Updates:** Modify the polyline (stretch, move vertices, scale) and watch the area text update automatically
- **Linked Objects:** The text and polyline are linked - deleting the polyline also deletes the text
- **Precision:** Area is calculated using AutoCAD's built-in getArea() method for accuracy

#### Tips:

- **Check If Closed:** If you get an error, use `PEDIT` → Select polyline → Choose "Close" option
- **Text Style:** The text uses your current AutoCAD text style
- **Layer:** Text is created on the current active layer
- **Editing:** You can move the text manually if needed, but it will update in place when the polyline changes
- **Multiple Areas:** Run the command multiple times for different polylines - each gets its own auto-updating label

#### Troubleshooting:

**"Polyline must be closed" error:**
- Use PEDIT command
- Select your polyline
- Type "C" for Close
- Press Enter
- Now run INSERTAREA again

**Text doesn't update:**
- The reactor should be active
- Try modifying a vertex (not just moving the whole polyline)
- If still not working, use INSERTAREA again to create a new linked text

---

### SUMLENGTH - Sum of Lengths Label

Inserts a text label showing the total sum of lengths for multiple selected curves (polylines, arcs, circles, lines). The text automatically updates when any of the monitored objects are modified, moved, stretched, or deleted.

#### How to Use:

1. **Type `SUMLENGTH`** in the AutoCAD command line

2. **Select curves** - Use any AutoCAD selection method:
   - Click individual objects (polylines, arcs, circles, lines)
   - Window or crossing selection
   - Press Enter when done selecting

3. **Specify text position** - Click where you want the text to appear
   - The command shows the calculated total before asking for position
   - Click anywhere in the drawing to place the text

#### What It Does:

- Calculates the sum of lengths for all selected curves (polylines, arcs, circles, lines)
- Shows the total in the command line before placement
- **Lets you choose where to place the text** - click any position
- **Formats with thousands separator** (e.g., "1,234.56" instead of "1234.56")
- **Adds unit suffix** based on drawing units (m, ft, cm, etc.)
- Links the text to ALL selected objects using a reactor
- **Automatic Updates:** When you modify ANY monitored object, the sum updates automatically
- **Smart Delete:** If you delete one object, the sum updates to show remaining objects
- **Auto-Delete:** If you delete ALL objects, the text is deleted too

#### Examples:

**Example 1: Perimeter Calculation**
- Draw 4 polylines representing the sides of an irregular shape
- Type `SUMLENGTH`
- Select all 4 polylines and press Enter
- Command shows: "Total length: 125.50 m"
- Click where you want the text to appear
- Stretch one side - the total updates automatically!

**Example 2: Cable Length Totals**
- Draw multiple polylines representing cable runs
- Type `SUMLENGTH`
- Select all cable polylines
- Click in a corner or title block area for the text placement
- **Result:** Text shows total cable length like "1,285.75 ft"
- Add a segment to one cable - total recalculates instantly

**Example 3: Pipe Network**
- Select multiple pipe segments (polylines)
- Type `SUMLENGTH`
- Command displays total, then lets you click placement position
- **Result:** Shows total pipe length "3,456.89 m" at your chosen location
- Delete one pipe - the sum updates to show remaining pipes only

#### Technical Details:

- **Text Position:** User-specified - you choose where to place the text
- **Preview:** Shows calculated total in command line before placement
- **Text Height:** Default 2.5 units
- **Text Format:** Decimal with 2 decimal places and thousands separator (e.g., "1,234.56 m")
- **Unit Detection:** Automatically detects drawing units (INSUNITS) and adds appropriate suffix:
  - Metric: mm, cm, m, km
  - Imperial: in, ft, yd, mi
  - Generic: units (if units are undefined)
- **Text Alignment:** Center-middle justified at chosen position
- **Multi-Object Monitoring:** Reactor watches ALL selected curves simultaneously
- **Supported Types:** Polylines, arcs, circles, lines, and any AcDbCurve-derived object
- **Dynamic Updates:** Any change to any monitored object triggers recalculation
- **Smart Deletion:** Removes deleted objects from tracking, updates sum with remaining

#### Important Notes:

- **Works with all curve types:** Polylines (open/closed), arcs, circles, lines, splines, ellipses
- **Visual Feedback:** When you select the sum text, all linked curves highlight automatically
- **Arc Length:** Arcs contribute their arc length to the sum
- **Circle Circumference:** Circles contribute their full circumference (2πr) to the sum
- **Line Length:** Straight lines contribute their linear distance
- **Real-Time Updates:** Modify, stretch, move vertices, or scale any monitored object - sum updates automatically
- **Linked Objects:** All selected curves are linked to the text
- **Partial Deletion:** Delete some objects - text updates to show remaining sum
- **Complete Deletion:** Delete all objects - text is automatically removed

#### Tips:

- **Selection Methods:** Use standard AutoCAD selection (window, crossing, individual picks)
- **Text Style:** Uses your current AutoCAD text style
- **Layer:** Text is created on the current active layer
- **Editing:** You can move the text manually if needed
- **Multiple Groups:** Run the command multiple times for different polyline groups
- **Status Message:** After insertion, shows how many polylines are being monitored

#### Troubleshooting:

**"No valid curves selected" error:**
- Make sure you selected curve objects (polylines, arcs, circles, lines)
- Text, blocks, or other non-curve objects won't work

**Sum doesn't include all objects:**
- Verify objects are curves using LIST command
- Some complex objects may need to be exploded

**Text doesn't update:**
- Try modifying an object directly (stretch, scale, etc.)
- The reactor monitors geometric changes to the curves
- If needed, use SUMLENGTH again to create a new linked text

---

## Text Manipulation Commands

### COPYTEXT - Copy Text Content
Copies the text content from one text object to another.

**Usage:**
1. Type `COPYTEXT`
2. Select the source text object (the one to copy FROM)
3. Select destination text objects (the ones to copy TO)
4. Press Enter to apply

**Works with:** Single-line text (TEXT), multi-line text (MTEXT)

---

### COPYSTYLE - Copy Text Style Only
Copies only the text style (font, effects) WITHOUT changing text height or content.

**Usage:**
1. Type `COPYSTYLE`
2. Select source text object
3. Select destination text objects
4. Press Enter to apply

**Note:** Does not work with dimensions - use COPYDIMSTYLE for dimensions

---

### COPYTEXTFULL - Copy Style and Dimensions
Copies both text style AND dimensions (height, width factor, etc.).

**Usage:**
1. Type `COPYTEXTFULL`
2. Select source text object
3. Select destination text objects
4. Press Enter to apply

**Copies:** Font, text style, height, width factor, oblique angle, and all other text properties

---

### COPYDIMSTYLE - Copy Dimension Style
Copies dimension style from one dimension to others.

**Usage:**
1. Type `COPYDIMSTYLE`
2. Select source dimension
3. Select destination dimensions
4. Press Enter to apply

**Works with:** All dimension types (linear, aligned, angular, radial, etc.)

---

## Polyline Tools

### CREATEPOLY - Create Polyline
Creates a polyline by selecting multiple line and arc segments.

### POLYCORNER - Round Polyline Corners
Adds rounded corners (fillets) to all vertices of a polyline.

**Usage:**
1. Type `POLYCORNER`
2. Select a polyline
3. Enter the radius for the corners

---

## Alignment Tools

### ALIGN3PT - Three-Point Alignment
Aligns an object using three reference points.

### HVALIGN - Horizontal Align
Aligns objects horizontally to a reference point.

### VVALIGN - Vertical Align
Aligns objects vertically to a reference point.

---

## Distribution Tools

### DISTVERT - Distribute Vertically
Distributes selected objects evenly along a vertical line with specified spacing.

**Usage:**
1. Type `DISTVERT`
2. Select objects to distribute
3. Enter the spacing distance

### DISTHORIZ - Distribute Horizontally
Distributes selected objects evenly along a horizontal line with specified spacing.

---

## Information Commands

### HWHELP - Show All Commands
Displays a categorized list of all available commands in the HelloWorld plugin.

### HWVERSION - Version Information
Shows plugin version, build date/time, file timestamp, and compiler information.

---

## Development Commands

If you're working with the plugin's source code, these commands are available in the LISP file:

- **RELOADHW** - Reload the plugin (automatically loads the newest version)
- **UNLOADHW** - Unload the plugin
- **RELOADHWBUILD** - Rebuild and reload the plugin
- **RELOADHWPATH** - Reload from a custom file path

---

## Tips and Best Practices

1. **Use HWHELP:** Type `HWHELP` anytime to see a quick reference of all commands

2. **Group Awareness:** Commands like COPYSTYLE work seamlessly with grouped objects (like those created by SEQNUM)

3. **Undo Available:** All commands support AutoCAD's standard UNDO command

4. **Selection Methods:** Most commands support multiple selection methods:
   - Click individual objects
   - Window selection (left-to-right)
   - Crossing selection (right-to-left)
   - Press Enter when done selecting

5. **Visual Feedback:** Watch the command line for prompts and confirmation messages

6. **Text Safety:** The plugin handles large text safely (up to 2048 characters) with automatic warnings

---

## Troubleshooting

**Command not recognized:**
- Make sure the plugin is loaded (`RELOADHW` if available)
- Check version with `HWVERSION`
- Type `HWHELP` to verify available commands

**Selection not working:**
- Ensure you're selecting the correct object type (text for text commands, dimensions for dimension commands)
- Check the command line for specific error messages

**Unexpected results:**
- Use AutoCAD's `U` command to undo
- Check object properties to ensure they're the expected type
- Verify current layer and text style settings

---

## Support

For issues or questions about this plugin, refer to:
- ARCHITECTURE.md (technical documentation for developers)
- README.md (project overview and setup)

---

*Plugin Version: Check with `HWVERSION` command*  
*Documentation Last Updated: January 2026*

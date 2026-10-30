// ---------------------------------------------------------------------------
// Snap-fit case for the BF-018A JJY antenna board with an M5Stack ATOM Lite
// ---------------------------------------------------------------------------
// Two parts, both print without supports on a Creality Ender 3 class printer:
//   base : tray the antenna PCB drops into (print as modelled, floor on bed)
//   lid  : cap that snaps over the base    (print upside down, top on bed)
//
// Every dimension of the PCB below was read from the KiCad Gerber/drill files
// in PCBV01L02/gerber/JLCPCB (board outline, 2.5 mm mounting holes, ATOM
// silkscreen outline, header pads). The ATOM Lite body, the USB-C/Grove side
// and the reset button come from M5Stack's official dimension drawing.
//
// Coordinate system: origin at the centre of the PCB, X to the right (the
// USB-C side of the ATOM), Y "north" (the 5-pin header side), Z up from the
// outside bottom of the base.
//
// Use the "part" variable (or the -D part=\"lid\" command line switch) to pick
// what to export.
// ---------------------------------------------------------------------------

part = "both"; // [base, lid, lid_print, both, assembly, section_x, section_y]

/* ---------------------------- printer / fit -------------------------------- */
pcb_clr    = 0.25;  // gap between PCB edge and cavity wall, per side
fit_clr    = 0.20;  // sliding gap between base lip and lid skirt, per side
snap_p     = 0.30;  // how far the snap ridges stand proud of the lip
snap_g     = 0.40;  // depth of the matching grooves in the lid skirt
chamfer    = 0.8;   // cosmetic chamfer on the outer top/bottom edges

/* ---------------------------- PCB (from Gerbers) --------------------------- */
pcb_w = 58.42;      // board width  (2.300 in)
pcb_h = 36.83;      // board height (1.450 in)
pcb_r = 2.54;       // corner radius
pcb_t = 1.60;       // board thickness (JLCPCB default)
hole_d = 2.5;       // the two non-plated mounting holes
hole1 = [-26.115, 15.240];   // 3.095 / 3.175 mm from the top-left corner
hole2 = [-10.875,  8.509];   // 18.335 / 9.906 mm from the top-left corner (coil centre)

/* ---------------------------- ATOM Lite ------------------------------------ */
atom_c    = [16.43, -5.715]; // centre of the ATOM (silkscreen outline), 0.78 mm from the right PCB edge
atom_w    = 24.0;            // body is 24 x 24 mm
atom_r    = 2.5;             // body corner radius
atom_h    = 9.5;             // body height (M5Stack datasheet)
atom_gap  = 2.54;            // the ATOM sits on the plastic of the 2.54 mm male headers
cap_c     = [24.685, 12.86]; // 100 uF electrolytic, 6.3 mm dia, 7.7 mm tall
cap_d     = 6.3;
cap_h     = 7.7;
reset_x   = 11.73;           // reset button: south face of the ATOM, 7.3 mm from its west edge
reset_zc  = 5.5;             // ...and this high above the ATOM's bottom face
usb_zc    = 7.3;             // USB-C port centre above the ATOM's bottom face

/* ---------------------------- case ----------------------------------------- */
wall      = 2.4;    // outer wall thickness (base below the seam, lid skirt + lip above it)
lip_t     = 1.0;    // inner lip of the base that the lid skirt slides over
lip_h     = 3.0;    // height of that lip
floor_t   = 1.6;
under_clr = 3.0;    // room under the PCB for the header solder tails
ledge_w   = 1.5;    // perimeter ledge the PCB rests on
seam_above_pcb = 1.0;   // base wall rises this far above the PCB top
lid_t     = 1.6;
lid_air   = 1.0;    // air between the ATOM's top and the lid underside
peg_d     = 2.2;    // locating pegs that go through the PCB holes
peg_above = 1.0;    // peg tip above the PCB top
standoff_d = 5.0;
boss_d    = 4.5;    // lid bosses that hold the PCB down and receive the peg tips
boss_hole_d = 2.8;
boss_hole_depth = 1.5;
boss_gap  = 0.2;    // boss stands this far above the PCB top
button_hole_d = 18; // opening above the ATOM's button / LED
notch_w   = 16.0;   // USB-C / Grove cable opening in the east wall
notch_clr = 0.2;
reset_w   = 3.5;    // reset access slot (width x height)
reset_h   = 6.0;
snap_len_long  = 14;
snap_len_short = 10;
snap_h    = 0.8;    // ridge height (z)
snap_z    = 1.3;    // ridge centre above the seam

/* ---------------------------- derived -------------------------------------- */
skirt_t     = wall - lip_t - fit_clr;
z_pcb_bot   = floor_t + under_clr;
z_pcb_top   = z_pcb_bot + pcb_t;
z_seam      = z_pcb_top + seam_above_pcb;
z_lip_top   = z_seam + lip_h;
z_atom_bot  = z_pcb_top + atom_gap;
z_atom_top  = z_atom_bot + atom_h;
z_lid_under = z_atom_top + lid_air;
z_lid_top   = z_lid_under + lid_t;
z_sill_top  = z_atom_bot - 0.5;     // east wall stays closed up to just under the ATOM
outer_off   = pcb_clr + wall;       // outer wall offset from the PCB edge
lip_off     = pcb_clr + lip_t;      // outer face of the lip
skirt_in    = lip_off + fit_clr;    // inner face of the lid skirt
ox = pcb_w/2 + outer_off;           // outer half sizes
oy = pcb_h/2 + outer_off;
eps = 0.01;

$fa = 2; $fs = 0.25;

echo(str("Outer size: ", 2*ox, " x ", 2*oy, " x ", z_lid_top, " mm"));
echo(str("PCB top z=", z_pcb_top, "  seam z=", z_seam, "  ATOM z=", z_atom_bot, "..", z_atom_top, "  lid underside z=", z_lid_under));

// rounded rectangle offset outward from the PCB outline by d
module pcb_rr(d = 0) {
    offset(r = pcb_r + d) square([pcb_w - 2*pcb_r, pcb_h - 2*pcb_r], center = true);
}

module rr(w, h, r) { offset(r = r) square([w - 2*r, h - 2*r], center = true); }

// extrusion of the outer outline with a 45 deg chamfer at the bottom and/or top
module outer_body(z0, z1, ch_bot = 0, ch_top = 0) {
    hull() {
        if (ch_bot > 0) {
            translate([0, 0, z0]) linear_extrude(eps) pcb_rr(outer_off - ch_bot);
            translate([0, 0, z0 + ch_bot]) linear_extrude(eps) pcb_rr(outer_off);
        } else translate([0, 0, z0]) linear_extrude(eps) pcb_rr(outer_off);
        if (ch_top > 0) {
            translate([0, 0, z1 - ch_top - eps]) linear_extrude(eps) pcb_rr(outer_off);
            translate([0, 0, z1 - eps]) linear_extrude(eps) pcb_rr(outer_off - ch_top);
        } else translate([0, 0, z1 - eps]) linear_extrude(eps) pcb_rr(outer_off);
    }
}

// triangular snap ridge, running along X, apex pointing +Y, centred at origin
module ridge(len) {
    rotate([90, 0, 90]) linear_extrude(len, center = true)
        polygon([[0, -snap_h/2], [snap_p, 0], [0, snap_h/2]]);
}

/* ============================== BASE ======================================= */
module base() {
    difference() {
        union() {
            // outer wall up to the seam
            outer_body(0, z_seam, ch_bot = chamfer);
            // lip
            translate([0, 0, z_seam - eps]) linear_extrude(lip_h + eps) pcb_rr(lip_off);
            // sill: east wall closed at full thickness up to just under the ATOM
            translate([pcb_w/2 + pcb_clr - eps, atom_c[1] - notch_w/2, z_seam - eps])
                cube([wall + eps, notch_w, z_sill_top - z_seam + eps]);
            // snap ridges on the lip (north, south, west)
            translate([0,  pcb_h/2 + lip_off - eps, z_seam + snap_z]) ridge(snap_len_long);
            translate([0, -(pcb_h/2 + lip_off - eps), z_seam + snap_z]) mirror([0, 1, 0]) ridge(snap_len_long);
            translate([-(pcb_w/2 + lip_off - eps), 0, z_seam + snap_z]) rotate([0, 0, 90]) ridge(snap_len_short);
        }
        // cavity
        translate([0, 0, floor_t]) linear_extrude(z_lip_top + 10) pcb_rr(pcb_clr);
        // open the lip above the sill for the Grove socket / USB-C housing
        translate([pcb_w/2 + pcb_clr - 1, atom_c[1] - notch_w/2, z_sill_top]) cube([10, notch_w, 20]);
        // lead-in chamfer on the outer top edge of the lip
        translate([0, 0, z_lip_top]) difference() {
            linear_extrude(1, center = true) pcb_rr(lip_off + 1);
            hull() {
                translate([0, 0, -0.5]) linear_extrude(eps) pcb_rr(lip_off);
                translate([0, 0, 0]) linear_extrude(eps) pcb_rr(lip_off - 0.4);
            }
        }
    }
    // ledge the PCB rests on
    translate([0, 0, floor_t - eps]) linear_extrude(under_clr + eps) difference() {
        pcb_rr(pcb_clr + eps);
        pcb_rr(pcb_clr - ledge_w);
    }
    // standoffs + locating pegs at the two mounting holes
    for (h = [hole1, hole2]) translate([h[0], h[1], floor_t - eps]) {
        cylinder(d = standoff_d, h = under_clr + eps);
        translate([0, 0, under_clr]) cylinder(d = peg_d, h = pcb_t + peg_above - 0.4);
        translate([0, 0, under_clr + pcb_t + peg_above - 0.4 - eps]) cylinder(d1 = peg_d, d2 = peg_d - 0.8, h = 0.4);
    }
}

/* ============================== LID ======================================== */
module lid() {
    difference() {
        outer_body(z_seam, z_lid_top, ch_top = chamfer);
        // inside of the skirt / under the plate
        translate([0, 0, z_seam - 1]) linear_extrude(z_lid_under - z_seam + 1) pcb_rr(skirt_in);
        // lead-in chamfer on the inner bottom edge of the skirt
        translate([0, 0, z_seam]) hull() {
            translate([0, 0, -eps]) linear_extrude(eps) pcb_rr(skirt_in + 0.4);
            translate([0, 0, 0.4]) linear_extrude(eps) pcb_rr(skirt_in);
        }
        // opening above the ATOM's button / LED, with a comfort chamfer on top
        translate([atom_c[0], atom_c[1], z_lid_under - 1]) cylinder(d = button_hole_d, h = lid_t + 2);
        translate([atom_c[0], atom_c[1], z_lid_top - 0.6]) cylinder(d1 = button_hole_d, d2 = button_hole_d + 1.2, h = 0.6 + eps);
        // USB-C / Grove cable notch through the east skirt and the plate edge above the ATOM
        translate([atom_c[0] + atom_w/2 - 0.25, atom_c[1] - notch_w/2 - notch_clr, z_seam - 1])
            cube([20, notch_w + 2*notch_clr, 30]);
        // reset button access slot in the south skirt
        translate([reset_x, -(pcb_h/2 + outer_off) - 1, z_atom_bot + reset_zc]) rotate([-90, 0, 0])
            hull() for (dz = [-1, 1]) translate([0, -dz*(reset_h - reset_w)/2, 0]) cylinder(d = reset_w, h = wall + 2);
        // snap grooves inside the skirt (north, south, west)
        for (s = [1, -1]) translate([0, s*(pcb_h/2 + skirt_in + snap_g/2 - eps), z_seam + snap_z])
            cube([snap_len_long + 1, snap_g + 2*eps, snap_h + 0.2], center = true);
        translate([-(pcb_w/2 + skirt_in + snap_g/2 - eps), 0, z_seam + snap_z])
            cube([snap_g + 2*eps, snap_len_short + 1, snap_h + 0.2], center = true);
    }
    // bosses that hold the PCB down and catch the peg tips
    for (h = [hole1, hole2]) translate([h[0], h[1], z_pcb_top + boss_gap]) difference() {
        cylinder(d = boss_d, h = z_lid_under - z_pcb_top - boss_gap + eps);
        translate([0, 0, -eps]) cylinder(d = boss_hole_d, h = boss_hole_depth + eps);
    }
}

/* ============================== MOCK-UPS (preview only) ==================== */
module mock_pcb() {
    color("green", 0.8) translate([0, 0, z_pcb_bot]) difference() {
        linear_extrude(pcb_t) pcb_rr(0);
        for (h = [hole1, hole2]) translate([h[0], h[1], -1]) cylinder(d = hole_d, h = pcb_t + 2);
    }
    color("silver") translate([cap_c[0], cap_c[1], z_pcb_top]) cylinder(d = cap_d, h = cap_h);
    // header pins (5 + 4) and their plastic
    color("black") {
        translate([13.89 - 1.27, 1.905 - 1.27, z_pcb_top]) cube([5*2.54, 2.54, 2.54]);
        translate([16.43 - 1.27, -13.335 - 1.27, z_pcb_top]) cube([4*2.54, 2.54, 2.54]);
    }
}
module mock_atom() {
    color("lightgray", 0.9) translate([atom_c[0], atom_c[1], z_atom_bot])
        linear_extrude(atom_h) rr(atom_w, atom_w, atom_r);
    // USB-C port + Grove socket on the east face
    color("gold") translate([atom_c[0] + atom_w/2 - 1, atom_c[1] - 4.45, z_atom_bot + usb_zc - 1.6]) cube([1.5, 8.9, 3.2]);
    color("white") translate([atom_c[0] + atom_w/2 - 4, atom_c[1] - 5.0, z_atom_bot]) cube([6, 10, 6.3]);
    // reset button
    color("red") translate([reset_x, atom_c[1] - atom_w/2 - 0.3, z_atom_bot + reset_zc]) cube([2, 0.6, 2], center = true);
}

/* ============================== OUTPUT ===================================== */
// lid turned over so that it prints on its top face (no supports needed)
module lid_print() { translate([0, 0, z_lid_top]) rotate([180, 0, 0]) lid(); }

if (part == "base") base();
if (part == "lid") lid();
if (part == "lid_print") lid_print();
if (part == "both") { base(); translate([0, 2*oy + 8, 0]) lid_print(); }
if (part == "assembly") { base(); color("steelblue", 0.6) lid(); mock_pcb(); mock_atom(); }
// cross sections through the ATOM centre (section_x: looking from the south, section_y: looking from the east)
if (part == "section_x") difference() {
    union() { base(); color("steelblue") lid(); mock_pcb(); mock_atom(); }
    translate([-100, -100 + atom_c[1], -1]) cube([200, 100, 100]);
}
// 2D cut drawings through the ATOM centre (for checking clearances)
if (part == "cut_x") projection(cut = true) rotate([90, 0, 0]) translate([0, -atom_c[1], 0]) { base(); lid(); mock_pcb(); mock_atom(); }
if (part == "cut_y") projection(cut = true) rotate([0, -90, 0]) rotate([0, 0, -90])  translate([-atom_c[0], 0, 0]) { base(); lid(); mock_pcb(); mock_atom(); }
if (part == "cut_y2") projection(cut = true) rotate([0, -90, 0]) rotate([0, 0, -90])  translate([-hole1[0], 0, 0]) { base(); lid(); mock_pcb(); mock_atom(); }
if (part == "mock_atom") mock_atom();
if (part == "mock_pcb") mock_pcb();
if (part == "section_y") difference() {
    union() { base(); color("steelblue") lid(); mock_pcb(); mock_atom(); }
    translate([atom_c[0], -100, -1]) cube([100, 200, 100]);
}

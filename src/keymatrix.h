/* TI-99/4A keyboard matrix encoding shared by the front end and the on-screen keyboard.
 * A key is encoded as column*8 + row (TMS9901 column select 0-5, row read-back bit 0-7). */
#ifndef KEYMATRIX_H
#define KEYMATRIX_H

#define K(c, r) ((uint8_t)((c) * 8 + (r)))
enum {
    K_EQ = K(0,0), K_SPACE = K(0,1), K_ENTER = K(0,2), K_FCTN = K(0,4), K_SHIFT = K(0,5), K_CTRL = K(0,6),
    K_PERIOD = K(1,0), K_L = K(1,1), K_O = K(1,2), K_9 = K(1,3), K_2 = K(1,4), K_S = K(1,5), K_W = K(1,6), K_X = K(1,7),
    K_COMMA = K(2,0), K_K = K(2,1), K_I = K(2,2), K_8 = K(2,3), K_3 = K(2,4), K_D = K(2,5), K_E = K(2,6), K_C = K(2,7),
    K_M = K(3,0), K_J = K(3,1), K_U = K(3,2), K_7 = K(3,3), K_4 = K(3,4), K_F = K(3,5), K_R = K(3,6), K_V = K(3,7),
    K_N = K(4,0), K_H = K(4,1), K_Y = K(4,2), K_6 = K(4,3), K_5 = K(4,4), K_G = K(4,5), K_T = K(4,6), K_B = K(4,7),
    K_SLASH = K(5,0), K_SEMI = K(5,1), K_P = K(5,2), K_0 = K(5,3), K_1 = K(5,4), K_A = K(5,5), K_Q = K(5,6), K_Z = K(5,7),
    K_NONE = 0xFF
};

#endif

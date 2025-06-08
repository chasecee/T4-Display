#pragma once

// Initializes the rotary encoder (uses GPIO22 for CLK, GPIO21 for DT)
void encoder_init(void);

// Returns signed detent steps since last call (CW positive, CCW negative)
int encoder_get_delta(void); 
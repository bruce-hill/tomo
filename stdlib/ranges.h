#pragma once

// Ranges represent numeric ranges

PUREFUNC Range_t Range$reversed(Range_t r);
PUREFUNC Range_t Range$by(Range_t r, Int_t step);

extern const TypeInfo Range$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1

// A datatype used for pattern match results

#pragma once

typedef struct {
    Text_t text;
    Int_t index;
    List_t captures;
} XMatch;

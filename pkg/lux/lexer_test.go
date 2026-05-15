package lux

import (
	"testing"
)

func TestLexerCombinators(t *testing.T) {
	source := "[ 42 ] 1 #:"
	l := NewLexer(source, false)
	tokens, err := l.Tokenize()
	if err != nil {
		t.Fatalf("Tokenize error: %v", err)
	}

	expected := []struct {
		typ TokenType
		val string
	}{
		{TokenLBracket, "["},
		{TokenNumber, "42"},
		{TokenRBracket, "]"},
		{TokenNumber, "1"},
		{TokenWord, "#:"},
		{TokenEOF, ""},
	}

	if len(tokens) != len(expected) {
		t.Fatalf("Expected %d tokens, got %d", len(expected), len(tokens))
	}

	for i, exp := range expected {
		if tokens[i].Type != exp.typ {
			t.Errorf("Token %d: expected type %d, got %d", i, exp.typ, tokens[i].Type)
		}
		if tokens[i].Value != exp.val {
			t.Errorf("Token %d: expected value %q, got %q", i, exp.val, tokens[i].Value)
		}
	}
}

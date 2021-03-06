#ifndef EXPRESSION_H__
#define EXPRESSION_H__

/*
 * {{ if (player.shots_made / person.shots_taken) / 100 > .5 }}
 * Holy cow!
 * {{ endif }}
 *
 * <keyword:if>
 * <symbol:oparen>
 * <identifier:player.shots_made>
 * <mathop:divide>
 * <identifier:player.shots_taken>
 * <symbol:cparen>
 * <mathop:divide>
 * <boolop:gt>
 * <number:.5>
 */

/*
 * After literal:
 *   - oparen
 *   - cparen
 *   - mathop
 *   - boolop
 *   - comma
 *   - cbracket (only inside sequence literal)
 */

typedef SSlice StringLiteral;

typedef mpd_t NumericLiteral;

typedef SSlice Identifier;

typedef enum {
    OPERAND_STRING,
    OPERAND_NUMBER,
    OPERAND_IDENTIFIER,
    OPERAND_EXPRESSION,
    OPERAND_MAX
} OperandType;

typedef struct {
    OperandType type;
    union {
        StringLiteral   string;
        NumericLiteral *number;
        Identifier      identifier;
        SSlice          expression;
    } as;
} Operand;

typedef enum {
    MATH_OPERAND_NUMBER,
    MATH_OPERAND_IDENTIFIER,
    MATH_OPERAND_EXPRESSION,
    MATH_OPERAND_MAX
} MathOperandType;

typedef struct {
    MathOperandType type;
    union {
        NumericLiteral *number;
        Identifier      identifier;
        SSlice          expression;
    } as;
} MathOperand;

typedef enum {
    RANGE_FLAG_BLANK,
    RANGE_FLAG_HAS_START,
    RANGE_FLAG_HAS_STEP
} RangeFlag;

typedef struct {
    RangeFlag   flags;
    MathOperand start;
    MathOperand stop;
    MathOperand step;
} Range;

typedef struct {
    Operand operand1;
    Operand operand2;
    MathOp  op;
} MathExpression;

typedef struct {
    Operand operand1;
    Operand operand2;
    BoolOp  op;
} BooleanExpression;

typedef struct {
    Operand     operand;
    UnaryBoolOp op;
} UnaryBooleanExpression;

typedef enum {
    EXPRESSION_NODE_STRING,
    EXPRESSION_NODE_NUMERIC,
    EXPRESSION_NODE_IDENTIFIER,
    EXPRESSION_NODE_RANGE,
    EXPRESSION_NODE_MAX
} ExpressionNodeType;

typedef struct {
    ExpressionNodeType type;
    union {
        StringLiteral     string;
        NumericLiteral   *number;
        Identifier        identifier;
        Range             range;
    } as;
} ExpressionNode;

#endif

/* vi: set et ts=4 sw=4: */


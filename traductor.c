
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_TOKENS 1000

typedef enum {
    L_LLAVE, R_LLAVE, L_CORCHETE, R_CORCHETE,
    COMA, DOS_PUNTOS,
    STRING, NUMBER,
    PR_TRUE, PR_FALSE, PR_NULL,
    EOF_TOKEN, ERROR_TOKEN
} TokenType;

const char* token_names[] = {
    "L_LLAVE", "R_LLAVE", "L_CORCHETE", "R_CORCHETE",
    "COMA", "DOS_PUNTOS", "STRING", "NUMBER",
    "PR_TRUE", "PR_FALSE", "PR_NULL", "EOF", "ERROR_TOKEN"
};

typedef struct {
    TokenType tipo;
    char lexema[1024];
    int linea;
} Token;

Token tokens[MAX_TOKENS];
int token_index = 0, current = 0;
FILE* salida;

// --------- ANALIZADOR LÉXICO ---------
void agregar_token(TokenType tipo, const char* lexema, int linea) {
    tokens[token_index].tipo = tipo;
    strcpy(tokens[token_index].lexema, lexema);
    tokens[token_index].linea = linea;
    token_index++;
}

void analizar_linea(char* linea, int numero_linea) {
    char *p = linea;
    while (*p) {
        if (isspace(*p)) {
            p++;
        } else if (strchr("[]{},:", *p)) {
            char simbolo[2] = {*p, '\0'};
            switch (*p) {
                case '[': agregar_token(L_CORCHETE, simbolo, numero_linea); break;
                case ']': agregar_token(R_CORCHETE, simbolo, numero_linea); break;
                case '{': agregar_token(L_LLAVE, simbolo, numero_linea); break;
                case '}': agregar_token(R_LLAVE, simbolo, numero_linea); break;
                case ',': agregar_token(COMA, simbolo, numero_linea); break;
                case ':': agregar_token(DOS_PUNTOS, simbolo, numero_linea); break;
            }
            p++;
        } else if (*p == '"') {
            char buffer[1024] = {0};
            char *inicio = p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
            strncpy(buffer, inicio, p - inicio);
            agregar_token(STRING, buffer, numero_linea);
        } else {
            char buffer[1024] = {0};
            int i = 0;
            while (*p && !isspace(*p) && !strchr("[]{},:", *p)) {
                buffer[i++] = *p++;
            }
            buffer[i] = '\0';

            if (strcasecmp(buffer, "true") == 0) agregar_token(PR_TRUE, buffer, numero_linea);
            else if (strcasecmp(buffer, "false") == 0) agregar_token(PR_FALSE, buffer, numero_linea);
            else if (strcasecmp(buffer, "null") == 0) agregar_token(PR_NULL, buffer, numero_linea);
            else {
                int esNumero = 1;
                for (int j = 0; buffer[j]; j++) {
                    if (!isdigit(buffer[j]) && buffer[j] != '.' && buffer[j] != 'e' && buffer[j] != 'E' && buffer[j] != '+' && buffer[j] != '-') {
                        esNumero = 0;
                        break;
                    }
                }
                if (esNumero) agregar_token(NUMBER, buffer, numero_linea);
                else agregar_token(ERROR_TOKEN, buffer, numero_linea);
            }
        }
    }
}

// --------- PARSER Y TRADUCTOR ---------
Token actual() { return tokens[current]; }
void avanzar() { if (current < token_index) current++; }

int aceptar(TokenType tipo) {
    if (actual().tipo == tipo) {
        avanzar();
        return 1;
    }
    return 0;
}

int esperar(TokenType tipo) {
    if (aceptar(tipo)) return 1;
    fprintf(stderr, "Error en línea %d: se esperaba %s, se encontró %s\n",
        actual().linea, token_names[tipo], token_names[actual().tipo]);
    // Panic mode: sincronizar saltando tokens hasta COMA o cierre
    while (actual().tipo != tipo && actual().tipo != COMA && actual().tipo != R_LLAVE && actual().tipo != R_CORCHETE && actual().tipo != EOF_TOKEN)
        avanzar();
    return 0;
}

int element();

int attribute_value() {
    Token t = actual();
    if (t.tipo == STRING || t.tipo == NUMBER || t.tipo == PR_TRUE || t.tipo == PR_FALSE || t.tipo == PR_NULL) {
        fprintf(salida, "%s", t.lexema);
        avanzar();
        return 1;
    } else {
        return element();
    }
}

int attribute() {
    Token nombre = actual();
    if (!esperar(STRING)) return 0;
    if (!esperar(DOS_PUNTOS)) return 0;
    fprintf(salida, "\t\t<%s>", nombre.lexema + 1); // sin la comilla
    attribute_value();
    fprintf(salida, "</%s>\n", nombre.lexema + 1);
    nombre.lexema[strlen(nombre.lexema) - 1] = '\0'; // quitar comilla final
    return 1;
}

int attributes_list() {
    if (!attribute()) return 0;
    while (aceptar(COMA)) {
        if (!attribute()) return 0;
    }
    return 1;
}

int object() {
    if (!esperar(L_LLAVE)) return 0;
    fprintf(salida, "\t<item>\n");
    if (aceptar(R_LLAVE)) {
        fprintf(salida, "\t</item>\n");
        return 1;
    }
    if (!attributes_list()) return 0;
    if (!esperar(R_LLAVE)) return 0;
    fprintf(salida, "\t</item>\n");
    return 1;
}

int element_list() {
    if (!element()) return 0;
    while (aceptar(COMA)) {
        if (!element()) return 0;
    }
    return 1;
}

int array() {
    if (!esperar(L_CORCHETE)) return 0;
    if (aceptar(R_CORCHETE)) return 1;
    if (!element_list()) return 0;
    return esperar(R_CORCHETE);
}

int element() {
    if (actual().tipo == L_LLAVE) return object();
    if (actual().tipo == L_CORCHETE) {
        fprintf(salida, "<hijos>\n");
        int ok = array();
        fprintf(salida, "</hijos>\n");
        return ok;
    }
    return 0;
}

int json() {
    fprintf(salida, "<personas>\n");
    int ok = element();
    fprintf(salida, "</personas>\n");
    return ok && actual().tipo == EOF_TOKEN;
}

// --------- MAIN PRINCIPAL ---------
int main() {
    FILE* entrada = fopen("fuente.txt", "r");
    salida = fopen("output.xml", "w");
    if (!entrada || !salida) {
        printf("Error al abrir fuente.txt o crear output.xml\n");
        return 1;
    }

    char linea[1024];
    int numero_linea = 1;
    while (fgets(linea, sizeof(linea), entrada)) {
        analizar_linea(linea, numero_linea++);
    }
    fclose(entrada);
    agregar_token(EOF_TOKEN, "EOF", numero_linea);

    if (json()) {
        printf("Traducción completada. Revisar output.xml\n");
    } else {
        printf("Errores encontrados durante la traducción\n");
    }

    fclose(salida);
    return 0;
}

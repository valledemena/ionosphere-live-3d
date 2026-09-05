#pragma once

// Mini-parser JSON ligero y sin dependencias, orientado a los formatos planos
// de NOAA SWPC (arrays de objetos con claves fijas) y al GeoJSON GloTEC.
//
// No es un parser JSON general: asume JSON bien formado y sin escapes en los
// strings que interesan. Extrae unos pocos campos concretos, suficiente para
// las fuentes del proyecto (SWPC, GloTEC).
#include <string>
#include <cctype>
#include <cstdlib>

namespace jmini {

// Salta espacios, tabuladores, nuevas lineas y signos ':' ',' 
static void skipWs(const char* s, size_t n, size_t& i) {
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ':' || c == ',') i++;
        else break;
    }
}

// Localiza el indice donde empieza el valor numerico del primer objeto cuyo
// campo 'key' existe. Asume que 'from' apunta al inicio del array '[' o al
// inicio de un objeto. Devuelve la posicion del primer digito/signo y deja en
// 'out' el valor. Devuelve false si no lo encuentra.
static bool firstNumberForKey(const std::string& s, const std::string& key, double& out) {
    size_t n = s.size();
    // Buscar cada '"'+key+'"' seguido de ':' -> valor numerico
    size_t i = 0;
    while (i + key.size() + 2 <= n) {
        // buscar comilla
        if (s[i] != '"') { i++; continue; }
        // leer string en comillas
        size_t j = i + 1;
        size_t kEnd = j;
        while (kEnd < n && s[kEnd] != '"') kEnd++;
        if (kEnd >= n) return false;
        std::string tok = s.substr(j, kEnd - j);
        if (tok == key) {
            // avanzar hasta ':'
            size_t col = kEnd + 1;
            while (col < n && s[col] != ':') col++;
            if (col >= n) { i = kEnd + 1; continue; }
            // saltar ws tras ':'
            size_t v = col + 1;
            while (v < n && (s[v] == ' ' || s[v] == '\t')) v++;
            // leer token numerico (eventualmente incluye exponente/signo)
            size_t startTok = v;
            while (v < n && (isdigit((unsigned char)s[v]) || s[v] == '.' ||
                             s[v] == 'e' || s[v] == 'E' || s[v] == '+' || s[v] == '-')) v++;
            if (v > startTok) {
                std::string num = s.substr(startTok, v - startTok);
                out = atof(num.c_str());
                return true;
            }
        }
        i = kEnd + 1;
    }
    return false;
}

// Version que devuelve el string de un valor (para claves tipo "0" o "1P").
static bool firstStringForKey(const std::string& s, const std::string& key, std::string& out) {
    size_t n = s.size();
    size_t i = 0;
    while (i + key.size() + 2 <= n) {
        if (s[i] != '"') { i++; continue; }
        size_t j = i + 1;
        size_t kEnd = j;
        while (kEnd < n && s[kEnd] != '"') kEnd++;
        if (kEnd >= n) return false;
        std::string tok = s.substr(j, kEnd - j);
        if (tok == key) {
            size_t col = kEnd + 1;
            while (col < n && s[col] != ':') col++;
            if (col >= n) { i = kEnd + 1; continue; }
            size_t v = col + 1;
            while (v < n && (s[v] == ' ' || s[v] == '\t')) v++;
            if (v < n && s[v] == '"') {
                size_t vEnd = v + 1;
                while (vEnd < n && s[vEnd] != '"') vEnd++;
                if (vEnd < n) { out = s.substr(v + 1, vEnd - v - 1); return true; }
            }
        }
        i = kEnd + 1;
    }
    return false;
}

// Cuenta cuantos objetos "{...}" hay en el documento (nivel 0).
static size_t countObjects(const std::string& s) {
    size_t n = s.size();
    size_t count = 0;
    int depth = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '"') {
            size_t j = i + 1;
            while (j < n && s[j] != '"') j++;
            i = j;  // saltar string
        } else if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) count++; }
    }
    return count;
}

}  // namespace jmini

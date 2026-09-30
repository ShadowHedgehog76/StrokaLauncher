#ifndef STROKA_JAVA_H
#define STROKA_JAVA_H

/* Télécharge (si besoin) le runtime Java officiel de Mojang et retourne le chemin de l'exécutable java.
 * component : ex. "java-runtime-delta" (Java 21). Résultat à libérer avec free. */
char *java_ensure(const char *runtime_root, const char *component);

#endif

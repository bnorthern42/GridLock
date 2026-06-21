#include <QRegularExpression>
#include <QString>
#include <iostream>

int main() {
    QString line = "*stopped,reason=\"breakpoint-hit\",frame={file=\"rank0.c\",line=\"10\"}";
    QRegularExpression rxFile("file=\"([^\"]+)\"");
    auto matchFile = rxFile.match(line);
    QString file = matchFile.hasMatch() ? matchFile.captured(1) : "";
    std::cout << "file: " << file.toStdString() << std::endl;
    return 0;
}

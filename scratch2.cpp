#include <QRegularExpression>
#include <QString>
#include <iostream>

int main() {
    QString line = "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"1\",frame={addr=\"0x0000555555555566\",func=\"main\",args=[{name=\"argc\",value=\"1\"},{name=\"argv\",value=\"0x7fffffffc4a8\"}],file=\"../tests/mpi_mm.c\",fullname=\"/home/bradn44/Projects/current/Gridlock/tests/mpi_mm.c\",line=\"83\",arch=\"i386:x86-64\"},thread-id=\"1\",stopped-threads=\"all\",core=\"16\"";
    QRegularExpression rxLine("frame=\\{.*?line=\"(\\d+)\""); // using .*? instead of [^}]*
    auto matchLine = rxLine.match(line);
    if (matchLine.hasMatch()) {
        std::cout << "Match: " << matchLine.captured(1).toStdString() << std::endl;
    } else {
        std::cout << "No match!" << std::endl;
    }
    return 0;
}

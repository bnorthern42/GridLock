#include <QRegularExpression>
#include <QString>
#include <iostream>

int main() {
    QString line = "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"1\",frame={addr=\"0x401000\",func=\"main\",args=[],file=\"mpi_mm.c\",fullname=\"/tmp/mpi_mm.c\",line=\"26\"}";
    QRegularExpression rxLine("frame=\\{[^}]*line=\"(\\d+)\"");
    auto matchLine = rxLine.match(line);
    if (matchLine.hasMatch()) {
        std::cout << matchLine.captured(1).toInt() << std::endl;
    } else {
        std::cout << "no match" << std::endl;
    }
    return 0;
}

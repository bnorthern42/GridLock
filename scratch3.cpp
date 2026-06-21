#include <QRegularExpression>
#include <QString>
#include <iostream>

int main() {
    QString line = "300^error,msg=\"-var-create: unable to create variable object\"\n";
    if (line.contains("^error")) {
        QRegularExpression errRe("30(\\d+)\\^error");
        auto match = errRe.match(line);
        if (match.hasMatch()) {
            std::cout << "Match: " << match.captured(1).toStdString() << std::endl;
        } else {
            std::cout << "No match!" << std::endl;
        }
    }
    return 0;
}

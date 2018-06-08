#include <QObject>
#include <QTextStream>
#include <QFile>
#include <QDateTime>
#include <QDebug>

class Log: public QObject {
	Q_OBJECT
	public :
		Log(QObject *parent, QString logname = "./qsshterm.log"):QObject(parent), logFilename(logname)  {
			open();
		}

		~Log() {
			file.close();
		}

		void debug(QString msg) {
		    QTextStream stream(&file);  
		    QString str("[DEBUG]");  
		    str += dateTime.currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");  
		    str += ":";  
		    str += msg;  
		    str += "\n";  
		    stream << str  ;  
		}

		void info(QString msg) {
		    QTextStream stream(&file);  
		    QString str("[INFO]");  
		    str += dateTime.currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");  
		    str += ":";  
		    str += msg;  
		    str += "\n";  
		    stream << str  ;  
		}

		void error(QString msg) {
		    QTextStream stream(&file);  
		    QString str("[ERROR]");  
		    str += dateTime.currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");  
		    str += ":";  
		    str += msg;  
		    str += "\n";  
		    stream << str  ;  
		}

	private:
		QString logFilename;
		QFile file;  
    	QDateTime dateTime;  
		void open()  
		{  
		    file.setFileName(logFilename);  
		    file.open(QIODevice::WriteOnly | QIODevice::Append);  
		    if(!file.isOpen())  
		    {  
		        qDebug() << "Open logfile fails";  
		    }  
		} 


};
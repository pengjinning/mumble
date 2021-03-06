
/* Copyright (C) 2005-2011, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef MUMBLE_MUMBLE_CONFIGWIDGET_H_
#define MUMBLE_MUMBLE_CONFIGWIDGET_H_

#include <QtCore/QtGlobal>
#include <QtCore/QObject>
#if QT_VERSION >= 0x050000
# include <QtWidgets/QWidget> 
#else 
# include <QtGui/QWidget>
#endif

struct Settings;
class ConfigDialog;
class QSlider;
class QAbstractButton;
class QComboBox;

class ConfigWidget : public QWidget {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(ConfigWidget)
	protected:
		void loadSlider(QSlider *, int);
		void loadCheckBox(QAbstractButton *, bool);
		void loadComboBox(QComboBox *, int);
	signals:
		void intSignal(int);
	public:
		Settings &s;
		ConfigWidget(Settings &st);
		virtual QString title() const = 0;
		virtual QIcon icon() const;
	public slots:
		virtual void accept() const;
		virtual void save() const = 0;
		virtual void load(const Settings &r) = 0;
		virtual bool expert(bool) = 0;
};

typedef ConfigWidget *(*ConfigWidgetNew)(Settings &st);

class ConfigRegistrar Q_DECL_FINAL {
		friend class ConfigDialog;
		friend class ConfigDialogMac;
	private:
		Q_DISABLE_COPY(ConfigRegistrar)
	protected:
		int iPriority;
		static QMap<int, ConfigWidgetNew> *c_qmNew;
	public:
		ConfigRegistrar(int priority, ConfigWidgetNew n);
		~ConfigRegistrar();
};

#endif

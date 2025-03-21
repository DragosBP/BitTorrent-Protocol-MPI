# Pasii Protocolului de BitTorrent (simulare locala folosind MPI)

## 1. Initializarea:
Toti userii afla ce fisiere au si descarca hash-urile segmentelor, apoi citesc de ce fisiere au nevoie.

## 2. Informarea
Cand au termiinat de descarcat(citit din fisier), trimit la tracker aceste informatii, care (trackerul) asteapta de la fiecare, rand pe rand, sa primeasca informatiile (si daca nu are niciun fisier, tot asteapta sa il anunte fiecare proces de ceva). Cand termina, trackerul da un broadcast la toate celelalte procese sa anunte ca pot incepe descarcarea (este folosit pe post de o bariera).

## 3. Descarcarea
Faza de descarcare este impartita in 3 sectiuni pe baza tagurilor:  
- **Tracker Tag**: Cu acesta se folosesc procesele de a afla de la tracker la cine se afla fisierul dorit (cere swarmul de fisiere).
- **Info Tag**: acesta este folosit de download sa trimita la uploaduri sa le intrebe daca au segmentul pe care il cauta. Uploadul asteapt pana primeste un mesaj de la orice proces si, cat timp nu este cel al trackerului, cauta segmentul cerut de catre download, apoi...
- **Data Tag**: cu acesta trimite uploadul un ACK/NACK (pentru hash-ul cerut) daca il are sau nu inapoi downloadului. Downloadul, dupa ce a trimi mesajul de Info, asteapta pana primeste un raspuns de la uploadul caruia i-a trimis.  

Pentru eficientizare cererilor si trimiterilor, download-urile cer, pe rand, un segment din fiecare fisier apoi trec la urmatorul s.a.m.d.**(Round Robin)**. Pentru fiecare fisier cerut, acestia trec prin fiecare membru din swarm sa ii ceara, necerand de 2 ori la rand acelasi segment de la aceiiasi persoana (doar daca au 1 singur fisier de descarcat care are un swarm de dimensiune 1).

Pe langa asta, la partea de dowload se mai afla si **UPDATE**, unde downloadul cere updatarea swarmului la tracker (la fiecare 10 segmente descarcate).


Trackerul, in toata partea de descarcare, asteapta cate un mesaj de la oricine, iar cand il primeste are 3 optiuni:
- **DOWNLOAD**: un proces a cerut informatiile despre hashurile segmentelor si swarmul unui fisier
- **UPDATE**: un proces a cerut informatiile despre swarmul unui fisier
- **DONE**: un proces a terminat de descarcat toate fisierele

## 4. Finalizare. 
In momentul in care un proces a terminat de descarcat toate fisierele, trimite trackerului un mesaj de **DONE** care, cand afla ca toate procesele au terminat, trimite un mesaj la update la toate procesele care, stiind ca atunci cand primesc un mesaj de la tracker inseamna ca au terminat treaba, se opresc. La download, dupa ce trimit mesajul de **DONE** trackerului, scriu segmentele fisierelor "descarcate" in fisiere si apoi se opresc si ele.
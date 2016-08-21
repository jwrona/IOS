#!/bin/bash
export LC_ALL=C

#####
function errprint
{ #funkce pro vypis napovedy pri spatne zadanych parametrech
  printf "Usage: $0 [-vtrsc] TEST_DIR [REGEX]

    -v  validate tree
    -t  run tests
    -r  report results
    -s  synchronize expected results
    -c  clear generated files

    It is mandatory to supply at least one option.\n" >&2
  exit 2
}

function checkfiles
{ #funkce pro kontrolu souboru ve stromu
  for file; do # kontrola souboru {stdin,stdout,stderr}-{expected,captured,delta}
    if [ -f "$file" ]; then
      if [ ! -w "$file" ]; then
	echo "WARNING: Soubor "$file" v adresari `pwd` neni zapisovatelny!" >&2 ; ret_value=1
      fi
    fi
  done

  # kontrola zadanych souboru na nepovolene znaky a pocet radku vetsi nez 1
  for file2 in status-expected status-captured; do
    if [ -f "$file2" ]; then
      if [ `cat "$file2" | grep -E -e "[^0-9]" | wc -l` -ge 1 ]; then
        echo "WARNING: Soubor "$file2" v adresari `pwd` obsahuje nepovolene znaky!" >&2 ; ret_value=1
      fi
      # test na pocet radku
      if [ `cat "$file2" | wc -l` -ne 1 ]; then
        echo "WARNING: Soubor "$file2" je ve spatnem formatu!" >&2 ; ret_value=1
      fi
    fi
  done

  # kontrola, zda se ve aktualnim adresari nachazeji nevyzadane soubory
  find . -maxdepth 1 -mindepth 1 ! -type d | while read line
  do
    for file3 in $* stdin-given cmd-given; do
      if [ "`basename "$line"`" = "$file3" ]; then
        break
      fi
      false
    done || { echo "WARNING: Soubor "`basename "$line"`" v adresari `pwd` je nevyzadany!" >&2 ; ret_value=1; }
    if [ "$ret_value" -eq 1 ]; then
      false
    fi
  done || ret_value=1
}

function report
{ # funkce zajistujici vykonani prikazu diff nad dvojici souboru {expected,captured},
  # a dle vysledku prikazu vypisuje vysledek na vystup zadany parametrem funkce
  descriptor="$1"

  for file in stdout stderr status
  do
    diff -up $file-expected $file-captured > $file-delta
    if [ "$?" -ge 2 ]; then
      rm "$file-delta"
    fi
  done

  # osetreni, zda je fd pripojen na terminal
  if [ -t "$descriptor" ]; then
    redb="\033[31m"
    rede="\033[0m"
    greenb="\033[32m"
    greene="\033[0m"
  fi

  for file in stdout stderr status
  do
    if [ -f "$file"-delta ]; then
      if [ -s "$file"-delta ]; then # test na nenulovou velikost delta souboru
	echo -e ""${path#./}": "$redb"FAILED"$rede"" >&"$descriptor"
	ret_value=1
	break
      fi
      false
    else
      echo -e ""${path#./}": "$redb"FAILED"$rede"" >&"$descriptor"
      ret_value=1
      break
    fi
  done || { echo -e ""${path#./}": "$greenb"OK""$greene" >&"$descriptor"; }
}
#####

OPTIND="0"
argv=""
argt=""
argr=""
args=""
argc=""
TEST_DIR=""
REGEX=""
export ret_value="0"

# zpracovani prepinacu utilitou getopts, nastavovani priznaku pro jednotlive operace
while getopts ":vtrsc" opt #dvojtecka na zacatku = nevypisuj chybove hlasky, dvojtecka za pismenem = prepinac bude mit parametr
do  case "$opt" in
      v)  argv=true;; #OPTIND = poradi prepinace (1. je nazev skriptu, 2. je prvni prepinac,...)
      t)  argt=true;;
      r)  argr=true;;
      s)  args=true;;
      c)  argc=true;;
      ?)  errprint;;  #Kdyz je zadan neznamy prepinac, getopts nastavi do $opt otaznik
    esac
done

if [ "$OPTIND" -eq 1 ]; then #test na nezadani zadnych prepinacu
  errprint
fi

((OPTIND--))  #V OPTIND je pocet platnych prepinacu + 1(jmeno skriptu). Zde je tento pocet dekrementovan 
shift $OPTIND #a zde se o jiz dekrementovany pocet posunou prepinace doleva.

if [ -n "$3" ]; then #test na zadani vice parametru nez je pozadovano
  errprint
fi

if [ "$1" ]; then #zda je zadan TEST_DIR
  TEST_DIR="$1"
  REGEX="$2"
else
  errprint
fi

if [ ! -d "$TEST_DIR" ]; then #pokud neni TEST_DIR adresar
  echo "ERROR: "$TEST_DIR" neni adresar!" >&2 ; exit 1
else
  cd $TEST_DIR || { echo "Nepovedlo se vstoupit do adresare "$TEST_DIR"!" >&2 ; exit 1; }
  TEST_DIR=`pwd`
fi

###################################################################################################
if [ "$argv" ]; then #akce vykonane pro prepinac -v

  # test na pritomnost symbolickych odkazu v celem stromu
  if [ `find . -type l | grep -E -e "$REGEX" | wc -l` -ne 0 ]; then
    echo "WARNING: Ve stromu jsou symbolicke odkazy!" >&2 ; ret_value=1
  fi

  # test na pritomnost vicenasobnych pecnych odkazu v celem stromu
  if [ `find . ! -type d ! -links 1 | grep -E -e "$REGEX" | wc -l` -ne 0 ]; then
    echo "WARNING: Ve stromu jsou vicenasobne pevne odkazy!" >&2 ; ret_value=1
  fi

  # nalezeni adresaru a jejich predani cyklu while, ktery do nich vstupuje a provadi akce
  find . -type d | grep -E -e "$REGEX" | sort | while read directory
  do
    cd "$directory" || { echo "ERROR: Nepovedlo se vstoupit do adresare "$directory"!" >&2 ; ret_value=1 ; continue; }

    # spocita pocet adresaru v adresari
    dir_count=`find . -maxdepth 1 -mindepth 1 -type d | wc -l`

    # pokud je pritomen alespon 1 adresar, testuje na (nezadouci) pritomnost souboru
    if [ "$dir_count" -ge 1 ]; then
      if [ `find . -maxdepth 1 -type f | wc -l` -ne 0 ]; then
	echo "WARNING: V adresari '`pwd`' jsou slozky i soubory!" >&2 ; ret_value=1
      fi
    fi

    # pokud nejsou pritomny adresare, testuje na pritomnost souboru cmd-given a nasledne jeho spustitelnost
    if [ "$dir_count" = 0 ]; then
      if [ ! -f cmd-given ]; then
	echo "WARNING: V adresari '`pwd`', ktery je bez vnorenych adresaru, neexistuje soubor cmd-given!" >&2 ; ret_value=1
      elif [ ! -x cmd-given ]; then
	echo "WARNING: V adresari '`pwd`', ktery je bez vnorenych adresaru, je soubor cmd-given nespustitelny!" >&2 ; ret_value=1
      fi
    fi

    # kontrola pritomnosti a citelnosti souboru stdin-given
    if [ -f "stdin-given" ];then
      if [ ! -r "stdin-given" ];then
	echo "WARNING: Soubor stdin-given v adresari `pwd` neni pristupny pro cteni!" >&2 ; ret_value=1
      fi
    fi

    # volani funkce pro kontrolu zadane mnoziny souboru
    checkfiles {stdout,stderr,status}-{expected,captured,delta}

    cd "$TEST_DIR" || { echo "ERROR: Nepovedlo se vystoupit z adresare `pwd`!" >&2 ; ret_value=1 ; break; }
    if [ "$ret_value" = 1 ]; then
      false
    fi
  done
  ret_value="$?"
fi

if [ "$argt" ]; then # akce vykonane pro prepinac -t

  # nalezeni souboru cmd-given ve stromu
  find . -type d | grep -E -e "$REGEX" | sort | while read path
  do
    if [ -f "$path"/cmd-given ]; then
      if [ -x "$path"/cmd-given ]; then
        cd "$path" || { echo "ERROR: Nepovedlo se vstoupit do adresare "$path"!" >&2 ; ret_value=1 ; continue; }

	if [ -f "stdin-given" ]; then # test na pritomnost a spustitelnost stdin-given
	  if [ -r "stdin-given" ]; then
	    stdin="stdin-given"
	  else
	    echo "WARNING: V adresari "$path" je soubor stdin-given necitelny!" >&2 ; ret_value=1
	    stdin="/dev/null"
	  fi
	else
	  stdin="/dev/null"
	fi

        # spusteni soboru cmd-given a potrebna presmerovani
	./cmd-given < "$stdin" > stdout-captured 2> stderr-captured
	echo "$?" > status-captured

        report 2 # volani funkce report s parametrem 2 (fd, na ktery bude funkce vypisovat hlaseni)

        cd "$TEST_DIR" || { echo "ERROR: Nepovedlo se vystoupit z adresare "$path"!" >&2 ; ret_value=1 ; break; }
      else
	echo "WARNING: V adresari "$path" je soubor cmd-given nespustitelny!" >&2 ; ret_value=1
      fi
    fi

    if [ "$ret_value" -ne 0 ]; then
      false
    fi

  done
  ret_value="$?"
fi

if [ "$argr" ]; then # akce vykonane pro prepinac -r
  find . -type d | grep -E -e "$REGEX" | sort | while read path
  do
    if [ -f "$path"/cmd-given ]; then
      if [ -x "$path"/cmd-given ]; then
        cd "$path" || { echo "ERROR: Nepovedlo se vstoupit do adresare "$path"!" >&2 ; ret_value=1 ; continue; }

        report 1 # volani funkce report s parametrem 1 (fd, na ktery bude funkce vypisovat hlaseni)

        cd "$TEST_DIR" || { echo "ERROR: Nepovedlo se vystoupit z adresare "$path"!" >&2 ; ret_value=1 ; break; }
      else
	echo "WARNING: V adresari "$path" je soubor cmd-given nespustitelny!" >&2 ; ret_value=1
      fi
    fi

    if [ "$ret_value" -ne 0 ]; then
      false
    fi

  done
  ret_value="$?"
fi

if [ "$args" ]; then # akce vykonane pro prepinac -s

  # hlavni cyklus pro nalezeni adresaru ve strumu dle regulerniho vyrazu
  find . -type d | grep -E -e "$REGEX" | sort | while read path
  do
    for file in stdout stderr status
    do # cyklus pro prejmenovani soboru -captured na -expected
      if [ -f "$path"/${file}-captured ]; then
        mv -f "$path"/${file}-captured "$path"/${file}-expected || { ret_value=1; }
      fi
    done
  done
fi

if [ "$argc" ]; then # akce vykonane pri prepinac -c
  find . -type d | grep -E -e "$REGEX" | sort | while read path
  do
    for file in stdout stderr status
    do # cyklus pro mazani soboru -captured a -delta
      if [ -f "$path"/${file}-captured ]; then
        rm "$path"/${file}-captured || { ret_value=1; }
      fi

      if [ -f "$path"/${file}-delta ]; then
        rm "$path"/${file}-delta || { ret_value=1; }
      fi
    done
  done
fi

exit "$ret_value"

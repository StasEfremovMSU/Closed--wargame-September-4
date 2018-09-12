using System.Collections;
using System.Collections.Generic;
using UnityEngine;


public class regStat : FarRegStat
{
    /// <summary>
    /// Контейнер с точками, куда будут нацелены
    /// </summary>
    public List<Vector3> targPos;

    /// <summary>
    /// Тип построения
    /// Кавалерия
    /// Легкая пехота
    /// Тяжелая пехота
    ///  </summary>
    public int Type;

    /// <summary>
    /// Тип построения
    /// Копейщики
    /// Стрелки
    /// Мечники
    /// Всадники
    ///  </summary>
    public int AddType;

    public bool senior_officer = false;
    public bool junior_officer = false;

    /// <summary>
    ///  Дополнение к повороту еси ноль то на угол по умолчанию
    /// </summary>
    public float delta_angle = 0;

    public bool moveCell = false;

    public bool standard_bearer = false;
    public bool trumpeter = false;

    public bool stateAtack;

    public float speed;

    public int cell_number;
    
    public int cellX;
    public int cellZ;

    // Хранение приказов
    public bool orderForw;
    public bool orderBack;
    public bool orderRotL;
    public bool orderRotW;
    
    /// <summary>
    /// где находится командная группа
    /// </summary>
    public int CommanderTypePosition; 

    /// <summary>
    /// Стреляем вместе
    /// </summary>
    public bool archTotewer;
    /// <summary>
    /// Стреляем по отдельности
    /// </summary>
    public bool archAlone;

    ///Местогде надо прописать new это loadRegiment
    /// <summary>
    /// Солдат в полку
    /// </summary>
    public int soldier_count;
    /// <summary>
    /// Первичное число солдат в полку
    /// </summary>
    public int prim_soldier_count;
    // Три параметра, по которым определяется построение:
    
    /// <summary>
    /// Тип построения
    /// </summary>
    public int typeRegiment = 1;

    /// <summary>
    /// Тип по составу полка
    /// </summary>
    public int typeComposition = 0;
    //0 - не определено

    /// <summary>
    /// Тип по расположения солдат в линиях
    /// </summary>
    public int typeSpecification = 0;

    //Блок переменных, отвечающих за параметры полка не по умолчанию

    /// <summary>
    /// Солдат в первом ряду
    /// </summary>
    public int regFirstLine;
    /// <summary>
    /// Солдат в первом ряду
    /// </summary>
    public int regLastLine;

    /// <summary>
    /// Где нахдятся крайние солдаты полка
    /// </summary>
    public Vector3[] Position_Reg; 

    /// <summary>
    /// В первом ряду клина
    /// </summary>
    public int regWegAddLine;

    /// <summary>
    /// Количество рядов
    /// </summary>
    public int regWegLeng;

    /// <summary>
    /// Расстояние между солдатами в шеренге, в линии/ в ряду
    /// </summary>
    public float regDistanseLine = 3;
    public float regDistanseRow = 3;

    /// <summary>
    /// Положение солдат относительно центра.
    /// </summary>
    public Vector2[,] matrix_pos;

    /// <summary>
    /// Угол в радианах, куда смотрит полк
    /// </summary>
    public float angle;

    public int min_num;
    public int max_num;

    public List<UnitSoldiers> soldiers;

    public List<UnitOfficer> officers;

    public Vector3 regPosition;

    /// Переменные состояния полка

    /// <summary>
    /// Находится ли в состоянии боя.
    /// </summary>
    public bool stateFighting;

    /// <summary>
    /// В панике
    /// </summary>
    public bool statePanic;

    /// <summary>
    /// Рассеянный полк
    /// </summary>
    public bool stateScattered;

    /// <summary>
    /// Массив состояний
    /// </summary>
    public bool[] state;

	/// <summary>
	/// Номер полка по списку загрузки из файла
	/// </summary>
	public int number;

    // К какой стороне конфликта относится полк
    public int command;

    // К какой схватке относится
    public int figthNumber;

    public float velocity;

    /// <summary>
    /// Номера полков с которыми сражается
    /// </summary>
    public List<int> figthMas;

    /// <summary>
    /// Номер полка, который атакует полк
    /// </summary>
    public int attack;

    /// <summary>
    /// Номер полка, который защищается
    /// </summary>
    public int defen;

    public Vector3 forw;

    /// <summary>
    /// Раздел с числами разных юнитов.
    /// </summary>
    public int numTypes;

    public int[] typeSoldiers;


    // Надо вызывать при формировании полка!
    /// <summary>
    /// Считает тип построения по составу
    /// </summary>
    public void countTypes()
    {
        // Определим количество типов в полку
        for (int i = 0; i < soldiers.Count; i++)
        {
            typeSoldiers[soldiers[i].unitType]++;
        }


        // Определим состав полка
        // Мечники-копейщики-лучники-арбалетчики-всадники-конныелучники
        // 0 - пусто 1 - мечники 2 - копейщики 3 - лучники 4 - арбалетчики

        if (typeSoldiers[0] == 0 && typeSoldiers[1] != 0 && typeSoldiers[2] != 0 && typeSoldiers[3] == 0 && typeSoldiers[4] == 0)
        {
            typeComposition = 1;// мечнико-копейщики
        }
        else
        {
            if (typeSoldiers[0] == 0 && typeSoldiers[1] != 0 && typeSoldiers[2] == 0 && typeSoldiers[3] != 0 && typeSoldiers[4] == 0)
            {
                typeComposition = 2;// мечнико-лучники
            }
            else
            {
                if (typeSoldiers[0] == 0 && typeSoldiers[1] == 0 && typeSoldiers[2] != 0 && typeSoldiers[3] != 0 && typeSoldiers[4] == 0)
                {
                    typeComposition = 3;// копейщики-лучники
                }
                else
                {
                    /// Тут дописать все другие возможные варианты сочетаний
                }
            }
        }
    }


    /// <summary>
    /// По имеющемуся массиву данных, расставляем солдат
    /// </summary>
    public void reLine()
    {
        int r;

        // Индекс для определения рядов в матрице положений
        int mr = 0; int ml = 0;
        float numRow;

        // Определим число шеренг
        numRow = (soldier_count - (soldier_count % regFirstLine)) / regFirstLine;

        for (int t = 0; t < soldiers.Count; t++)
        {
            if (ml == regFirstLine) { ml = 0; mr++; }

            matrix_pos[ml, mr].x = (soldiers[t].tempForRebuilding - 0.5f - regFirstLine / 2) * regDistanseLine;

            // ГДЕ ЧЕРТ ПОДЕРИ ЦЕНТР ПОЛКА?!
            // центр в середине первой строки, проверено
            matrix_pos[ml, mr].y = (soldiers[t].tempForRebuilding - (soldiers[t].tempForRebuilding % regFirstLine)) / regFirstLine * regDistanseRow;

            ml++;
        }

    }



    /// <summary>
    ///  Здесь вызваем обстрел врага с разными модификатора для каждого солдата
    /// </summary>
    public void archerAt(int NumReg, int NumRegTarg, bool togewer)
    {

        float distance = 0; // Расстояние до полка - цели
        distance = (mainStatic.regiment_stat[NumReg].regPosition - mainStatic.regiment_stat[NumRegTarg].regPosition).magnitude;

        // есть ли прямая видмость, пока есть, но потом добавить рейкасты
        bool DirectView = false;
        // кроме этого, надо понять по направлению
        Vector3 v, b; 
        v = (mainStatic.regiment_stat[NumRegTarg].regPosition - mainStatic.regiment_stat[NumReg].regPosition).normalized;
        b = (mainStatic.regiment_stat[NumRegTarg].soldiers[0].transform.position + mainStatic.regiment_stat[NumRegTarg].soldiers[0].transform.forward).normalized;
        /// Если в зоне видимости нулевого солдата, то находится в прямлой видимости
        if (Mathf.Abs(Vector3.Angle(v, b)) < 45)
        {
            DirectView = true;
        }

  

        // Одно время для залпа
        float RegTipe = Random.Range(0, 1000) / 1000.0f * mainStatic.TACTICPERIOD;// +0.3f;

        for (int t = 0; t < mainStatic.regiment_stat[NumReg].soldiers.Count; t++)
        {
               ArchBuf temp = new ArchBuf();

               if (mainStatic.regiment_stat[NumReg].soldiers[t].archer == true) // Если он стрелок, добавляем стрелу, производим выстрел
               {

                   //Если стрелок - добавляем стрелу

                   
                   int r = 0;///Атакованный человек
                   r = Random.Range(0, mainStatic.regiment_stat[NumRegTarg].soldiers.Count);

                   /// определяем три параметра для улетевшей стрелы
                   temp.stPos = mainStatic.regiment_stat[NumReg].soldiers[t].gameObject.transform.position;
                   temp.forw = Quaternion.LookRotation ((mainStatic.regiment_stat[NumRegTarg].soldiers[r].gameObject.transform.position - mainStatic.regiment_stat[NumReg].soldiers[t].gameObject.transform.position).normalized + mainStatic.regiment_stat[NumReg].soldiers[t].gameObject.transform.up);
                   float dis = (mainStatic.regiment_stat[NumRegTarg].soldiers[r].gameObject.transform.position - mainStatic.regiment_stat[NumReg].soldiers[t].gameObject.transform.position).magnitude;
                   temp.speed = Mathf.Pow(2, 0.5f) * Mathf.Pow(dis * 9.8f /2, 0.5f);

                   //ArrowBuff.put(temp);

                   if (archerCheme.makeBum(mainStatic.regiment_stat[NumReg].soldiers[t].FirstLine,
                    mainStatic.regiment_stat[NumReg].soldiers[t].getObject().GetComponent<UnitArcher>().archerType,
                    mainStatic.regiment_stat[NumReg].soldiers[t].getObject().GetComponent<UnitArcher>().archerSkill,
                    distance,
                    DirectView) == true)
                   {
                       // Если атака успешная

                       if (Random.Range(0, 20) > (mainStatic.regiment_stat[NumRegTarg].soldiers[r].defence - mainStatic.regiment_stat[NumReg].soldiers[t].getObject().GetComponent<UnitArcher>().archerPower))
                       {
                           // Если пробиты доспехи, то надо добавить

                           DeathBuf p;
                           p = new DeathBuf();
                           p.regNum = NumRegTarg;
                           p.solNum = r;

                           if (togewer == true)
                           {
                               /// Если стреляют все вместе
                               p.time = RegTipe;
                               DeadBuff.put(p);

                               temp.time = RegTipe - dis / temp.speed;
                               Debug.Log(temp.time);
                               ArrowBuff.put(temp);
                           }
                           else
                           {
                               p.time = Random.Range(0, 1000) / 1000.0f * mainStatic.TACTICPERIOD;
                               DeadBuff.put(p);

                               temp.time = p.time - dis / temp.speed;
                               Debug.Log(temp.time);
                               ArrowBuff.put(temp);
                               /// Если стреляют по очереди
                           }
                       }
                   }
               }

        }

        Debug.Log("------");
        for (int u = 0; u < ArrowBuff.archBuf.Count; u++)
        {
            //Debug.Log(ArrowBuff.archBuf[u].time);
            //Debug.Log(ArrowBuff.archBuf[u].speed);
        }
        Debug.Log("------");

    }

    
    // Функция в которой происходит расчет, убит ли враг при такой атаке и защите
    public bool archerTest(float at, float def, float m, int enemyNumReg, int enemyNumSol )
    {
            if (Random.Range(0, 20) > (def - m))
            {
                DeathBuf r;
                r = new DeathBuf();
                r.regNum = enemyNumReg;
                r.solNum = enemyNumSol;
                r.time = Random.Range(0, 1000) / 1000.0f * mainStatic.TACTICPERIOD;
                DeadBuff.put(r);

                return true; // убит
            }
        return false; // жив
    }


    /// <summary>
    /// Перестроение полка
    /// </summary>
    public void rebuilding(int ww )
    {

        for (int i = 0; i < soldiers.Count; i++)
        {
            soldiers[i].tempForRebuilding = 0;
        }

            switch (ww) ///!!!!!!!!!!!!!!!!не то
            {
                // Тип построения - в линию
                case (1):
                    switch (typeComposition)
                    {
                        case (1):
                            //мечники-копейщики
                            switch (typeSpecification)
                            {
                                case (0):
                                    // Описание специализации: в первых рядах - копейщики, потом в любом порядке мечники
                                    REBFULLSOMETHING(2);
                                    REBRELINE();
                                    break;

                                case (1):
                                    REBFULLSOMETHING(1);
                                    REBRELINE();
                                    break;
                            }
                            break;

                       case (2):
                            //мечники-лучники
                            switch (typeSpecification)
                            {
                                case (0):
                                    // Описание специализации: в первых рядах - мечники, потом в любом порядке лучники
                                    REBFULLSOMETHING(1);
                                    REBRELINE();
                                    break;

                                case (1):
                                    // Впереди лучники - потом мечники
                                    REBFULLSOMETHING(3);
                                    REBRELINE();
                                    break;
                            }
                            break;

                      case (3):
                            // копейщики-лучники
                            switch (typeSpecification)
                            {
                                case (0):
                                    // Описание специализации: в первых рядах - копейщик, потом в любом порядке лучники
                                    REBFULLSOMETHING(2);
                                    REBRELINE();
                                    break;

                                case (1):
                                    // Впереди лучники - потом копейщики
                                    REBFULLSOMETHING(3);
                                    REBRELINE();
                                    break;
                            }
                            break;
                    }

                    break;

                case (2):

                    switch (typeComposition)
                    {

                        case (1):
                            //мечники-копейщики
                            switch (typeSpecification)
                            {
                                case (0):
                                    // Описание специализации: в первых рядах - копейщики, потом в любом порядке мечники
                                    REBFULLSOMETHING(2);
                                    REBREWEG();
                                    MakeLastRawCenter();
                                    break;

                                case (1):
                                    // Впереди мечники - потом копейщики
                                    REBFULLSOMETHING(1);
                                    REBREWEG();
                                    MakeLastRawCenter();
                                    break;
                            }
                            break;

                        case (2):
                            //мечники-лучники
                            switch (typeSpecification)
                            {
                                case (0):
                                    // Описание специализации: в первых рядах - мечники, потом в любом порядке лучники
                                    REBFULLSOMETHING(2);
                                    REBREWEG();
                                    break;

                                case (1):
                                    // Впереди лучники - потом мечники
                                    REBFULLSOMETHING(3);
                                    REBREWEG();
                                    break;
                            }
                            break;

                        case (3):
                            // копейщики-лучники
                            switch (typeSpecification)
                            {
                                case (0):
                                    // Описание специализации: в первых рядах - копейщик, потом в любом порядке лучники
                                    REBFULLSOMETHING(2);
                                    REBREWEG();
                                    break;

                                case (1):
                                    // Впереди лучники - потом копейщики
                                    REBFULLSOMETHING(3);
                                    REBREWEG();
                                    break;
                            }
                            break;
                    }


                    break;


            }


    }

    /// <summary>
    /// Функция, которая заполняет типом k первые ряды
    /// </summary>
    /// <param name="k"></param>
    /// <param name="l"></param>
    void REBFULLSOMETHING(int k)
    {
        int c = 0;
        for (int i = 0; i < soldiers.Count; i++)
        {
            if (soldiers[i].unitType == k)
            {
                soldiers[i].tempForRebuilding = c;
                c++;
            }
        }
        for (int i = 0; i < soldiers.Count; i++)
        {
            if (soldiers[i].unitType != k)
            {
                soldiers[i].tempForRebuilding = c;
                c++;
            }
        }
    }




    void REBRELINE()
    {
        if (regFirstLine > 4)
        {
            int l = 0; int h = 0;
            // число офицеров
            int of_count = officers.Count;
            int tempof = 0;
            int of = 0;

            CommanderTypePosition = 5;

            for (int j = 0; j < soldiers.Count; j++)
            {
                l = (soldiers[j].tempForRebuilding + of ) % regFirstLine  ;
                h = (soldiers[j].tempForRebuilding + of  ) / regFirstLine;

                bool flag = true;
                switch (CommanderTypePosition)
                {
                    case (0):
                        if (of < of_count)
                        if (j < of_count)
                        {
                            Debug.Log(of);
                            officers[of].GetSoldiers().matrix_position = NewSolPos(l, h);
                            of++;
                            flag = false;

                        }
                        break;

                    case (1):
                        if (of < of_count)
                        if (j > regFirstLine / 2 - of_count && j < regFirstLine / 2 + of_count)
                        {
                            officers[of].GetSoldiers().matrix_position = NewSolPos(l, h);
                            of++;
                            flag = false;
                        }
                        break;
                    case (2):
                        if (of < of_count)
                            if (j > regFirstLine  - of_count-1 && j < regFirstLine)
                            {
                                officers[of].GetSoldiers().matrix_position = NewSolPos(l, h);
                                of++;
                                flag = false;
                            }
                        break;

                    case (3):
                        if (of < of_count)
                            if (j > (soldiers.Count - soldiers.Count % regFirstLine - 1) - regFirstLine)
                            {
                                officers[of].GetSoldiers().matrix_position = NewSolPos(l, h);
                                of++;
                                flag = false;
                            }
                        break;

                    case (4):
                        if (of < of_count)
                            if (j > (soldiers.Count - soldiers.Count % regFirstLine - 1) - regFirstLine/2 - of_count/2)
                            {
                                officers[of].GetSoldiers().matrix_position = NewSolPos(l, h);
                                of++;
                                flag = false;
                            }
                        break;

                    case (5):
                        if (of < of_count)
                            if (j > (soldiers.Count - soldiers.Count % regFirstLine - of_count - 1))
                            {
                                officers[of].GetSoldiers().matrix_position = NewSolPos(l, h);
                                of++;
                                flag = false;
                            }
                        break;

                }

                if (flag == true)
                {
                    soldiers[j].k1 = l;
                    soldiers[j].k2 = h;
                    soldiers[j].matrix_position = NewSolPos(l, h);
                }
                else
                {
                    j--;

                }

                l++;
            }
        }
           
    }


    void REBREWEG()
    {

        soldiers.Sort(delegate(UnitSoldiers x, UnitSoldiers y)
        {
            if (x.tempForRebuilding == null && y.tempForRebuilding == null)
                return 0;
            else if (x.tempForRebuilding < y.tempForRebuilding) return 1;
            else if (x.tempForRebuilding > y.tempForRebuilding) return -1;
            else return 0;
        });

        float line = regFirstLine = 8;// Длина текущей линии
        int ccount = 0; /// Солдат в линии уже стоит
        int rawcount = 0; // число линий
        /// На сколько увеличивается ряд
        float d = 2;
        /// Массив, куда складываем наращивание
        float buff = 0;
        int of = 0;
        int of_count = officers.Count;
        int l = 0;
        CommanderTypePosition = 4;

        for (int i = 0; i < soldiers.Count; i++)
        {

            float x = 0; float z = 0;
            x = l + -line / 2 * regDistanseLine + ccount * regDistanseLine;
            z = -rawcount * regDistanseRow;
            soldiers[i].k2 = ccount;
            soldiers[i].k2 = rawcount;
            soldiers[i].matrix_position = new Vector3(x, 0, z);


            bool flag = true;
            switch (CommanderTypePosition)
            {
                case (1):
                    if (of < of_count)
                        if (i > regFirstLine / 2 - of_count/2 )
                        {
                            officers[of].GetSoldiers().matrix_position = new Vector3(x, 0, z);
                            of++;
                            flag = false;
                        }
                    break;
                case (3):

                    /// Солдат в полку
                    int tt = 0;
                    float dtemp = 0; int rr = 0;
                    bool fl = true;
                    int res = 0;


                    while (soldiers.Count - (regFirstLine + rr) > tt)
                    {
                        res = tt;

                        if (dtemp >= 1)
                        {
                            tt = tt + regFirstLine + (int)dtemp + rr;
                            rr += (int)dtemp;
                            dtemp = d;
                        }
                        else
                        {
                            tt = tt + regFirstLine + rr;
                            dtemp += d;
                        }
                        if (tt > soldiers.Count)
                        {
                            tt = 99999;
                        }

                    }

                    if (of < of_count)
                        if (i > res - 1 - of_count)
                        {
                            officers[of].GetSoldiers().matrix_position = new Vector3(x, 0, z);
                            of++;
                            flag = false;
                        }
                    break;

                case (4):
                    tt = 0;
                    dtemp = 0; rr = 0;
                    fl = true;
                    /// Последний ря
                    res = 0;
                    /// Длина последнего ряда
                    int resrr = 0;


                    while (soldiers.Count - (regFirstLine + rr) > tt)
                    {
                        res = tt;
                        resrr = regFirstLine + rr;
                        if (dtemp >= 1)
                        {
                            tt = tt + regFirstLine + (int)dtemp + rr;
                            rr += (int)dtemp;
                            res++;
                            dtemp = d;
                        }
                        else
                        {
                            tt = tt + regFirstLine + rr;
                            dtemp += d;
                            res++;
                        }
                        if (tt > soldiers.Count)
                        {
                            tt = 99999;
                        }

                    }

                    if (of < of_count)
                        if (i > tt - 1 - of_count/2 - resrr /2)
                        {
                            officers[of].GetSoldiers().matrix_position = new Vector3(x, 0, z);
                            of++;
                            flag = false;
                        }
                    break;
                case (5):
                    tt = 0;
                    dtemp = 0; rr = 0;
                    fl = true;
                    /// Последний ряд
                    res = 0;
                    /// Длина последнего ряда
                    resrr = 0;


                    while (soldiers.Count - (regFirstLine + rr) > tt)
                    {
                        res = tt;
                        resrr = regFirstLine + rr;
                        if (dtemp >= 1)
                        {
                            tt = tt + regFirstLine + (int)dtemp + rr;
                            rr += (int)dtemp;
                            res++;
                            dtemp = d;
                        }
                        else
                        {
                            tt = tt + regFirstLine + rr;
                            dtemp += d;
                            res++;
                        }
                        if (tt > soldiers.Count)
                        {
                            tt = 99999;
                        }

                    }
                    if (of < of_count)
                        if (i > res - resrr)
                        {
                            officers[of].GetSoldiers().matrix_position = new Vector3(x, 0, z);
                            of++;
                            flag = false;
                        }
                    break;
            }

            if (flag == true)
            {
                soldiers[i].matrix_position = new Vector3(x, 0, z);
            }
            else
            {
                i--;

            }

            ccount++;

            if (ccount + l > line)
            {
                ccount = 0;
                rawcount++;
                // Если увеличиваем на 1 и более то все просто
                if (d >= 1)
                {
                    line += d;
                }
                else
                {
                    /// иначе мы должны "накопить на уширение ряда"
                    buff += d;
                    if (buff >= 1)
                    {
                        line += buff * 2;
                        buff = 0;
                    }
                    else
                    {
                        line = line;
                    }
                }
            }
        }
    }


    /// Возвращает new_poition  по ряду и линии 
    Vector3 NewSolPos(int l , int h)
    {
        Vector3 res;
        Vector3 v, v2; float tx = 0; float tz = 0;
        v.y = 0f;
        v.x = l * regDistanseLine;
        v.z = h * regDistanseRow * (-1);
        tx = v.x - regFirstLine / 2 * regDistanseLine; tz = v.z;
        float a =  angle;
        /// Сначала расставляем, потом поворачиваем на a
        v.x = tx * Mathf.Cos(a) + tz * Mathf.Sin(a);
        v.z = -tx * Mathf.Sin(a) + tz * Mathf.Cos(a);
        v2.x = v.x + regPosition.x;
        v2.y = 0f;
        v2.z = v.z + regPosition.z;
        res = v2;
        return res;
    }


    void MakeLastRawCenter()
    {
        /// Номер ряда в котором будем переставлять (последний)
        int raw = 0;
        int line = 0;
        /// Находим последний ряд
        for (int i = 0; i < soldiers.Count; i++)
        {
            if (soldiers[i].k2 > raw)
            {
                raw = soldiers[i].k2;
            }
            if (soldiers[i].k1 > line)
            {
                line = soldiers[i].k1;
            }
        }

        Vector3 temp = new Vector3 (0, 0, 0);

        int number = 0;

        /// Находим центр масс заднего ряда
        for (int i = 0; i < soldiers.Count; i++)
        {
            if (soldiers[i].k2 == raw)
            {
                temp += soldiers[i].matrix_position; 
                number++;
            }
        }
        temp = temp / number;
        /// Находим центр масс предпоследнего ряда
        Vector3 truetemp = new Vector3 (0, 0, 0);
        number = 0;
        for (int i = 0; i < soldiers.Count; i++)
        {
            if (soldiers[i].k2 == raw - 1 )
            {
                truetemp += soldiers[i].matrix_position; 
                number++;
            }
        }
        truetemp = truetemp / number;

        float move = truetemp.x - temp.x;

        /// Делаем коррекцию 
        for (int i = 0; i < soldiers.Count; i++)
        {
            if (soldiers[i].k2 == raw)
            {

                soldiers[i].matrix_position += new Vector3(move, 0, 0);
            }
        }
    }


    /// <summary>
    /// (пересчет)Четыре координаты по углам полка
    /// </summary>
    public void make_position()
    {
        if (regFirstLine * 2 <  soldier_count)
        {
            regLastLine = regFirstLine;
            Position_Reg[0] = soldiers[1].gameObject.transform.position;
            Position_Reg[1] = soldiers[regFirstLine - 1].gameObject.transform.position;
            Position_Reg[2] = soldiers[soldier_count - 1].gameObject.transform.position;
            Position_Reg[3] = soldiers[soldier_count - regLastLine].gameObject.transform.position;
        }
        else
        {
            Position_Reg[0] = soldiers[0].gameObject.transform.position;
            Position_Reg[1] = soldiers[soldier_count - 1].gameObject.transform.position;
            Position_Reg[2] = soldiers[0].gameObject.transform.position;
            Position_Reg[3] = soldiers[soldier_count - 1].gameObject.transform.position;
        }
    }


    /// <summary>
    ///  Пересчитываем скорость полка по самому медленному солдату или офицеру
    /// </summary>
    public void recount_speed()
    {
        float x = speed;
        for (int i = 0; i < soldiers.Count; i++)
        {
            if (x < soldiers[i].speed)
                x = soldiers[i].speed;
        }
        for (int i = 0; i < officers.Count; i++)
        {
            if (x < officers[i].GetSoldiers().speed)
                x = officers[i].GetSoldiers().speed;
        }
        speed = x;
    }

}
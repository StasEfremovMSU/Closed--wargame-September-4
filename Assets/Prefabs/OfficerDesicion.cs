using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class OfficerDesicion {


    static  void  make_desicion(int reg_num , int off_num )
    {
        /// Проверяем какие переменные
        ///  Если рядом враг - атакуем его
        ///  Рассматриваем ответную реакцию
        int temp = func.getNearFromList(reg_num, off_num, 2, 1);
        if (temp != 99999)
        {
            /// Если обороняющийся полк уже обороняется, то уже не добавляют ему
            if (mainStatic.regiment_stat[temp].stateFighting == false)
                ///Добавим атакованному номер атакующего
                mainStatic.regiment_stat[temp].defen = reg_num;

            /// Добавим атакующему номер атакуемого
            mainStatic.regiment_stat[reg_num].attack = temp;
        }


    }

	
}
